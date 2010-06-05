/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=4 sw=4 et tw=99:
 *
 * ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla SpiderMonkey JavaScript 1.9 code, released
 * May 28, 2008.
 *
 * The Initial Developer of the Original Code is
 *   Brendan Eich <brendan@mozilla.org>
 *
 * Contributor(s):
 *   David Anderson <danderson@mozilla.com>
 *   David Mandelin <dmandelin@mozilla.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#define __STDC_LIMIT_MACROS

#include "jscntxt.h"
#include "jsscope.h"
#include "jsobj.h"
#include "jslibmath.h"
#include "jsiter.h"
#include "jsnum.h"
#include "jsxml.h"
#include "jsstaticcheck.h"
#include "jsbool.h"
#include "assembler/assembler/MacroAssemblerCodeRef.h"
#include "jsiter.h"
#include "jstypes.h"
#include "methodjit/StubCalls.h"
#include "jstracer.h"
#include "jspropertycache.h"
#include "jspropertycacheinlines.h"
#include "jsscopeinlines.h"
#include "jsscriptinlines.h"
#include "jsstrinlines.h"
#include "jsobjinlines.h"
#include "jscntxtinlines.h"
#include "jsatominlines.h"

#include "jsautooplen.h"

using namespace js;
using namespace js::mjit;
using namespace JSC;

#define THROW()  \
    do {         \
        void *ptr = JS_FUNC_TO_DATA_PTR(void *, JaegerThrowpoline); \
        f.setReturnAddress(ReturnAddressPtr(FunctionPtr(ptr))); \
        return;  \
    } while (0)

#define THROWV(v)       \
    do {                \
        void *ptr = JS_FUNC_TO_DATA_PTR(void *, JaegerThrowpoline); \
        f.setReturnAddress(ReturnAddressPtr(FunctionPtr(ptr))); \
        return v;       \
    } while (0)

JSObject * JS_FASTCALL
mjit::stubs::BindName(VMFrame &f)
{
    PropertyCacheEntry *entry;

    /* Fast-path should have caught this. See comment in interpreter. */
    JS_ASSERT(f.fp->scopeChain->getParent());

    JSAtom *atom;
    JSObject *obj2;
    JSContext *cx = f.cx;
    JSObject *obj = f.fp->scopeChain->getParent();
    JS_PROPERTY_CACHE(cx).test(cx, f.regs.pc, obj, obj2, entry, atom);
    if (!atom)
        return obj;

    jsid id = ATOM_TO_JSID(atom);
    obj = js_FindIdentifierBase(cx, f.fp->scopeChain, id);
    if (!obj)
        THROWV(NULL);
    return obj;
}

static bool
InlineReturn(JSContext *cx)
{
    bool ok = true;

    JSStackFrame *fp = cx->fp;

    JS_ASSERT(!fp->blockChain);
    JS_ASSERT(!js_IsActiveWithOrBlock(cx, fp->scopeChain, 0));

    if (fp->script->staticLevel < JS_DISPLAY_SIZE)
        cx->display[fp->script->staticLevel] = fp->displaySave;

    // Marker for debug support.
    void *hookData = fp->hookData;
    if (JS_UNLIKELY(hookData != NULL)) {
        JSInterpreterHook hook;
        JSBool status;

        hook = cx->debugHooks->callHook;
        if (hook) {
            /*
             * Do not pass &ok directly as exposing the address inhibits
             * optimizations and uninitialised warnings.
             */
            status = ok;
            hook(cx, fp, JS_FALSE, &status, hookData);
            ok = (status == JS_TRUE);
            // CHECK_INTERRUPT_HANDLER();
        }
    }

    fp->putActivationObjects(cx);

    /* :TODO: version stuff */

    if (fp->flags & JSFRAME_CONSTRUCTING && fp->rval.isPrimitive())
        fp->rval = fp->thisv;

    cx->stack().popInlineFrame(cx, fp, fp->down);
    cx->regs->sp[-1] = fp->rval;

    return ok;
}

void * JS_FASTCALL
mjit::stubs::Return(VMFrame &f)
{
    if (!f.inlineCallCount)
        return f.fp->ncode;

    JSContext *cx = f.cx;
    JS_ASSERT(f.fp == cx->fp);

#ifdef DEBUG
    bool wasInterp = f.fp->script->ncode == JS_UNJITTABLE_METHOD;
#endif

    bool ok = InlineReturn(cx);

    f.inlineCallCount--;
    JS_ASSERT(f.regs.sp == cx->regs->sp);
    f.fp = cx->fp;

    JS_ASSERT_IF(f.inlineCallCount > 1 && !wasInterp,
                 f.fp->down->script->isValidJitCode(f.fp->ncode));

    if (!ok)
        THROWV(NULL);

    return f.fp->ncode;
}

static jsbytecode *
FindExceptionHandler(JSContext *cx)
{
    JSStackFrame *fp = cx->fp;
    JSScript *script = fp->script;

top:
    if (cx->throwing && script->trynotesOffset) {
        // The PC is updated before every stub call, so we can use it here.
        unsigned offset = cx->regs->pc - script->main;

        JSTryNoteArray *tnarray = script->trynotes();
        for (unsigned i = 0; i < tnarray->length; ++i) {
            JSTryNote *tn = &tnarray->vector[i];
            if (offset - tn->start >= tn->length)
                continue;
            if (tn->stackDepth > cx->regs->sp - fp->base())
                continue;

            jsbytecode *pc = script->main + tn->start + tn->length;
            JSBool ok = js_UnwindScope(cx, tn->stackDepth, JS_TRUE);
            JS_ASSERT(cx->regs->sp == fp->base() + tn->stackDepth);

            switch (tn->kind) {
                case JSTRY_CATCH:
                  JS_ASSERT(js_GetOpcode(cx, fp->script, pc) == JSOP_ENTERBLOCK);

#if JS_HAS_GENERATORS
                  /* Catch cannot intercept the closing of a generator. */
                  if (JS_UNLIKELY(cx->exception.isMagic(JS_GENERATOR_CLOSING)))
                      break;
#endif

                  /*
                   * Don't clear cx->throwing to save cx->exception from GC
                   * until it is pushed to the stack via [exception] in the
                   * catch block.
                   */
                  return pc;

                case JSTRY_FINALLY:
                  /*
                   * Push (true, exception) pair for finally to indicate that
                   * [retsub] should rethrow the exception.
                   */
                  cx->regs->sp[0].setBoolean(true);
                  cx->regs->sp[1] = cx->exception;
                  cx->regs->sp += 2;
                  cx->throwing = JS_FALSE;
                  return pc;

                case JSTRY_ITER:
                {
                  /*
                   * This is similar to JSOP_ENDITER in the interpreter loop,
                   * except the code now uses the stack slot normally used by
                   * JSOP_NEXTITER, namely regs.sp[-1] before the regs.sp -= 2
                   * adjustment and regs.sp[1] after, to save and restore the
                   * pending exception.
                   */
                  AutoValueRooter tvr(cx, cx->exception);
                  JS_ASSERT(js_GetOpcode(cx, fp->script, pc) == JSOP_ENDITER);
                  cx->throwing = JS_FALSE;
                  ok = !!js_CloseIterator(cx, cx->regs->sp[-1]);
                  cx->regs->sp -= 1;
                  if (!ok)
                      goto top;
                  cx->throwing = JS_TRUE;
                  cx->exception = tvr.value();
                }
            }
        }
    }

    return NULL;
}

extern "C" void *
js_InternalThrow(VMFrame &f)
{
    JSContext *cx = f.cx;

    // Make sure sp is up to date.
    JS_ASSERT(cx->regs == &f.regs);

    jsbytecode *pc = NULL;
    for (;;) {
        pc = FindExceptionHandler(cx);
        if (pc)
            break;

        // If |f.inlineCallCount == 0|, then we are on the 'topmost' frame (where
        // topmost means the first frame called into through js_Interpret). In this
        // case, we still unwind, but we shouldn't return from a JS function, because
        // we're not in a JS function.
        bool lastFrame = (f.inlineCallCount == 0);
        js_UnwindScope(cx, 0, cx->throwing);
        if (lastFrame)
            break;

        JS_ASSERT(f.regs.sp == cx->regs->sp);
        f.scriptedReturn = stubs::Return(f);
    }

    JS_ASSERT(f.regs.sp == cx->regs->sp);

    if (!pc) {
        *f.oldRegs = f.regs;
        f.cx->setCurrentRegs(f.oldRegs);
        return NULL;
    }

    return cx->fp->script->pcToNative(pc);
}

#define NATIVE_SET(cx,obj,sprop,entry,vp)                                     \
    JS_BEGIN_MACRO                                                            \
        if (sprop->hasDefaultSetter() &&                                      \
            (sprop)->slot != SPROP_INVALID_SLOT &&                            \
            !obj->scope()->brandedOrHasMethodBarrier()) {                     \
            /* Fast path for, e.g., plain Object instance properties. */      \
            obj->setSlot(sprop->slot, *vp);                                   \
        } else {                                                              \
            if (!js_NativeSet(cx, obj, sprop, false, vp))                     \
                THROW();                                                      \
        }                                                                     \
    JS_END_MACRO

static inline JSObject *
ValueToObject(JSContext *cx, Value *vp)
{
    if (vp->isObject())
        return &vp->asObject();
    if (!js_ValueToNonNullObject(cx, *vp, vp))
        return NULL;
    return &vp->asObject();
}

#define NATIVE_GET(cx,obj,pobj,sprop,getHow,vp)                               \
    JS_BEGIN_MACRO                                                            \
        if (sprop->hasDefaultGetter()) {                                      \
            /* Fast path for Object instance properties. */                   \
            JS_ASSERT((sprop)->slot != SPROP_INVALID_SLOT ||                  \
                      !sprop->hasDefaultSetter());                            \
            if (((sprop)->slot != SPROP_INVALID_SLOT))                        \
                *(vp) = (pobj)->lockedGetSlot((sprop)->slot);                 \
            else                                                              \
                (vp)->setUndefined();                                         \
        } else {                                                              \
            if (!js_NativeGet(cx, obj, pobj, sprop, getHow, vp))              \
                return NULL;                                                  \
        }                                                                     \
    JS_END_MACRO

void JS_FASTCALL
mjit::stubs::SetName(VMFrame &f, uint32 index)
{
    JSContext *cx = f.cx;

    Value &rref = f.regs.sp[-1];
    Value &lref = f.regs.sp[-2];
    JSObject *obj = ValueToObject(cx, &lref);
    if (!obj)
        THROW();

    do {
        PropertyCache *cache = &JS_PROPERTY_CACHE(cx);

        /*
         * Probe the property cache, specializing for two important
         * set-property cases. First:
         *
         *   function f(a, b, c) {
         *     var o = {p:a, q:b, r:c};
         *     return o;
         *   }
         *
         * or similar real-world cases, which evolve a newborn native
         * object predicatably through some bounded number of property
         * additions. And second:
         *
         *   o.p = x;
         *
         * in a frequently executed method or loop body, where p will
         * (possibly after the first iteration) always exist in native
         * object o.
         */
        PropertyCacheEntry *entry;
        JSObject *obj2;
        JSAtom *atom;
        if (cache->testForSet(cx, f.regs.pc, obj, &entry, &obj2, &atom)) {
            /*
             * Fast property cache hit, only partially confirmed by
             * testForSet. We know that the entry applies to regs.pc and
             * that obj's shape matches.
             *
             * The entry predicts either a new property to be added
             * directly to obj by this set, or on an existing "own"
             * property, or on a prototype property that has a setter.
             */
            JS_ASSERT(entry->vword.isSprop());
            JSScopeProperty *sprop = entry->vword.toSprop();
            JS_ASSERT_IF(sprop->isDataDescriptor(), sprop->writable());
            JS_ASSERT_IF(sprop->hasSlot(), entry->vcapTag() == 0);

            JSScope *scope = obj->scope();
            JS_ASSERT(!scope->sealed());

            /*
             * Fastest path: check whether the cached sprop is already
             * in scope and call NATIVE_SET and break to get out of the
             * do-while(0). But we can call NATIVE_SET only if obj owns
             * scope or sprop is shared.
             */
            bool checkForAdd;
            if (!sprop->hasSlot()) {
                if (entry->vcapTag() == 0 ||
                    ((obj2 = obj->getProto()) &&
                     obj2->isNative() &&
                     obj2->shape() == entry->vshape())) {
                    goto fast_set_propcache_hit;
                }

                /* The cache entry doesn't apply. vshape mismatch. */
                checkForAdd = false;
            } else if (!scope->isSharedEmpty()) {
                if (sprop == scope->lastProperty() || scope->hasProperty(sprop)) {
                  fast_set_propcache_hit:
                    PCMETER(cache->pchits++);
                    PCMETER(cache->setpchits++);
                    NATIVE_SET(cx, obj, sprop, entry, &rref);
                    break;
                }
                checkForAdd = sprop->hasSlot() && sprop->parent == scope->lastProperty();
            } else {
                /*
                 * We check that cx own obj here and will continue to
                 * own it after js_GetMutableScope returns so we can
                 * continue to skip JS_UNLOCK_OBJ calls.
                 */
                JS_ASSERT(CX_OWNS_OBJECT_TITLE(cx, obj));
                scope = js_GetMutableScope(cx, obj);
                JS_ASSERT(CX_OWNS_OBJECT_TITLE(cx, obj));
                if (!scope)
                    THROW();
                checkForAdd = !sprop->parent;
            }

            uint32 slot;
            if (checkForAdd &&
                entry->vshape() == cx->runtime->protoHazardShape &&
                sprop->hasDefaultSetter() &&
                (slot = sprop->slot) == scope->freeslot) {
                /*
                 * Fast path: adding a plain old property that was once
                 * at the frontier of the property tree, whose slot is
                 * next to claim among the allocated slots in obj,
                 * where scope->table has not been created yet.
                 *
                 * We may want to remove hazard conditions above and
                 * inline compensation code here, depending on
                 * real-world workloads.
                 */
                PCMETER(cache->pchits++);
                PCMETER(cache->addpchits++);

                /*
                 * Beware classes such as Function that use the
                 * reserveSlots hook to allocate a number of reserved
                 * slots that may vary with obj.
                 */
                if (slot < obj->numSlots() &&
                    !obj->getClass()->reserveSlots) {
                    ++scope->freeslot;
                } else {
                    if (!js_AllocSlot(cx, obj, &slot))
                        THROW();
                }

                /*
                 * If this obj's number of reserved slots differed, or
                 * if something created a hash table for scope, we must
                 * pay the price of JSScope::putProperty.
                 *
                 * (A reserveSlots hook can cause scopes of the same
                 * shape to have different freeslot values. This is
                 * what causes the slot != sprop->slot case. See
                 * js_GetMutableScope.)
                 */
                if (slot != sprop->slot || scope->table) {
                    JSScopeProperty *sprop2 =
                        scope->putProperty(cx, sprop->id,
                                           sprop->getter(), sprop->setter(),
                                           slot, sprop->attributes(),
                                           sprop->getFlags(), sprop->shortid);
                    if (!sprop2) {
                        js_FreeSlot(cx, obj, slot);
                        THROW();
                    }
                    sprop = sprop2;
                } else {
                    scope->extend(cx, sprop);
                }

                /*
                 * No method change check here because here we are
                 * adding a new property, not updating an existing
                 * slot's value that might contain a method of a
                 * branded scope.
                 */
                TRACE_2(SetPropHit, entry, sprop);
                obj->lockedSetSlot(slot, rref);

                /*
                 * Purge the property cache of the id we may have just
                 * shadowed in obj's scope and proto chains. We do this
                 * after unlocking obj's scope to avoid lock nesting.
                 */
                js_PurgeScopeChain(cx, obj, sprop->id);
                break;
            }
            PCMETER(cache->setpcmisses++);
            atom = NULL;
        } else if (!atom) {
            /*
             * Slower property cache hit, fully confirmed by testForSet (in
             * the slow path, via fullTest).
             */
            JSScopeProperty *sprop = NULL;
            if (obj == obj2) {
                sprop = entry->vword.toSprop();
                JS_ASSERT(sprop->writable());
                JS_ASSERT(!obj2->scope()->sealed());
                NATIVE_SET(cx, obj, sprop, entry, &rref);
            }
            if (sprop)
                break;
        }

        if (!atom)
            atom = f.fp->script->getAtom(index);
        jsid id = ATOM_TO_JSID(atom);
        if (entry && JS_LIKELY(obj->map->ops->setProperty == js_SetProperty)) {
            uintN defineHow;
            JSOp op = JSOp(*f.regs.pc);
            if (op == JSOP_SETMETHOD)
                defineHow = JSDNP_CACHE_RESULT | JSDNP_SET_METHOD;
            else if (op == JSOP_SETNAME)
                defineHow = JSDNP_CACHE_RESULT | JSDNP_UNQUALIFIED;
            else
                defineHow = JSDNP_CACHE_RESULT;
            if (!js_SetPropertyHelper(cx, obj, id, defineHow, &rref))
                THROW();
        } else {
            if (!obj->setProperty(cx, id, &rref))
                THROW();
        }
    } while (0);

    f.regs.sp[-2] = f.regs.sp[-1];
}

static void
ReportAtomNotDefined(JSContext *cx, JSAtom *atom)
{
    const char *printable = js_AtomToPrintableString(cx, atom);
    if (printable)
        js_ReportIsNotDefined(cx, printable);
}

static JSObject *
NameOp(VMFrame &f, uint32 index)
{
    JSContext *cx = f.cx;
    JSStackFrame *fp = f.fp;
    JSObject *obj = fp->scopeChain;

    JSScopeProperty *sprop;
    Value rval;

    PropertyCacheEntry *entry;
    JSObject *obj2;
    JSAtom *atom;
    JS_PROPERTY_CACHE(cx).test(cx, f.regs.pc, obj, obj2, entry, atom);
    if (!atom) {
        if (entry->vword.isFunObj()) {
            f.regs.sp++;
            f.regs.sp[-1].setFunObj(entry->vword.toFunObj());
            return obj;
        }

        if (entry->vword.isSlot()) {
            uintN slot = entry->vword.toSlot();
            JS_ASSERT(slot < obj2->scope()->freeslot);
            f.regs.sp++;
            f.regs.sp[-1] = obj2->lockedGetSlot(slot);
            return obj;
        }

        JS_ASSERT(entry->vword.isSprop());
        sprop = entry->vword.toSprop();
        goto do_native_get;
    }

    jsid id;
    id = ATOM_TO_JSID(atom);
    JSProperty *prop;
    if (!js_FindPropertyHelper(cx, id, true, &obj, &obj2, &prop))
        return NULL;
    if (!prop) {
        /* Kludge to allow (typeof foo == "undefined") tests. */
        JSOp op2 = js_GetOpcode(cx, f.fp->script, f.regs.pc + JSOP_NAME_LENGTH);
        if (op2 == JSOP_TYPEOF) {
            f.regs.sp++;
            f.regs.sp[-1].setUndefined();
            return obj;
        }
        ReportAtomNotDefined(cx, atom);
        return NULL;
    }

    /* Take the slow path if prop was not found in a native object. */
    if (!obj->isNative() || !obj2->isNative()) {
        obj2->dropProperty(cx, prop);
        if (!obj->getProperty(cx, id, &rval))
            return NULL;
    } else {
        sprop = (JSScopeProperty *)prop;
  do_native_get:
        NATIVE_GET(cx, obj, obj2, sprop, JSGET_METHOD_BARRIER, &rval);
        obj2->dropProperty(cx, (JSProperty *) sprop);
    }

    f.regs.sp++;
    f.regs.sp[-1] = rval;
    return obj;
}

void JS_FASTCALL
stubs::Name(VMFrame &f, uint32 index)
{
    if (!NameOp(f, index))
        THROW();
}

void JS_FASTCALL
stubs::GetElem(VMFrame &f)
{
    JSContext *cx = f.cx;
    JSFrameRegs &regs = f.regs;

    Value lval = regs.sp[-2];
    Value rval = regs.sp[-1];
    const Value *copyFrom;

    JSObject *obj;
    jsid id;
    int i;

    if (lval.isString() && rval.isInt32()) {
        Value retval;
        JSString *str = lval.asString();
        i = rval.asInt32();

        if ((size_t)i >= str->length())
            THROW();

        str = JSString::getUnitString(cx, str, (size_t)i);
        if (!str)
            THROW();
        f.regs.sp[-2].setString(str);
        return;
    }

    obj = ValueToObject(cx, &lval);
    if (!obj)
        THROW();

    if (rval.isInt32()) {
        if (obj->isDenseArray()) {
            jsuint idx = jsuint(rval.asInt32());
            
            if (idx < obj->getArrayLength() &&
                idx < obj->getDenseArrayCapacity()) {
                copyFrom = obj->addressOfDenseArrayElement(idx);
                if (!copyFrom->isMagic())
                    goto end_getelem;
                /* Otherwise, fall to getProperty(). */
            }
        } else if (obj->isArguments()
#ifdef JS_TRACER
                   && !GetArgsPrivateNative(obj)
#endif
                  ) {
            uint32 arg = uint32(rval.asInt32());

            if (arg < obj->getArgsLength()) {
                JSStackFrame *afp = (JSStackFrame *) obj->getPrivate();
                if (afp) {
                    copyFrom = &afp->argv[arg];
                    goto end_getelem;
                }

                copyFrom = obj->addressOfArgsElement(arg);
                if (!copyFrom->isMagic())
                    goto end_getelem;
                /* Otherwise, fall to getProperty(). */
            }
        }
        id = INT_TO_JSID(rval.asInt32());

    } else {
        if (!js_InternNonIntElementId(cx, obj, rval, &id))
            THROW();
    }

    if (!obj->getProperty(cx, id, &rval))
        THROW();
    copyFrom = &rval;

  end_getelem:
    f.regs.sp[-2] = *copyFrom;
}

static inline bool
FetchElementId(VMFrame &f, JSObject *obj, const Value &idval, jsid &id, Value *vp)
{
    int32_t i_;
    if (ValueFitsInInt32(idval, &i_)) {
        id = INT_TO_JSID(i_);
        return true;
    }
    return !!js_InternNonIntElementId(f.cx, obj, idval, &id, vp);
}

void JS_FASTCALL
stubs::SetElem(VMFrame &f)
{
    JSContext *cx = f.cx;
    JSFrameRegs &regs = f.regs;

    Value &objval = regs.sp[-3];
    Value &idval  = regs.sp[-2];
    Value retval  = regs.sp[-1];
    
    JSObject *obj;
    jsid id;

    obj = ValueToObject(cx, &objval);
    if (!obj)
        THROW();

    if (!FetchElementId(f, obj, idval, id, &regs.sp[-2]))
        THROW();

    if (obj->isDenseArray() && JSID_IS_INT(id)) {
        jsuint length = obj->getDenseArrayCapacity();
        jsint i = JSID_TO_INT(id);
        
        if ((jsuint)i < length) {
            if (obj->getDenseArrayElement(i).isMagic(JS_ARRAY_HOLE)) {
                if (js_PrototypeHasIndexedProperties(cx, obj))
                    goto mid_setelem;
                if ((jsuint)i >= obj->getArrayLength())
                    obj->setDenseArrayLength(i + 1);
                obj->incDenseArrayCountBy(1);
            }
            obj->setDenseArrayElement(i, regs.sp[-1]);
            goto end_setelem;
        }
    }

  mid_setelem:
    if (!obj->setProperty(cx, id, &regs.sp[-1]))
        THROW();

  end_setelem:
    /* :FIXME: Moving the assigned object into the lowest stack slot
     * is a temporary hack. What we actually want is an implementation
     * of popAfterSet() that allows popping more than one value;
     * this logic can then be handled in Compiler.cpp. */
    regs.sp[-3] = retval;
}

void JS_FASTCALL
stubs::CallName(VMFrame &f, uint32 index)
{
    JSObject *obj = NameOp(f, index);
    if (!obj)
        THROW();
    f.regs.sp++;
    f.regs.sp[-1].setNonFunObj(*obj);
}

void JS_FASTCALL
stubs::BitOr(VMFrame &f)
{
    int32_t i, j;

    if (!ValueToECMAInt32(f.cx, f.regs.sp[-2], &i) ||
        !ValueToECMAInt32(f.cx, f.regs.sp[-1], &j)) {
        THROW();
    }
    i = i | j;
    f.regs.sp[-2].setInt32(i);
}

void JS_FASTCALL
stubs::BitXor(VMFrame &f)
{
    int32_t i, j;

    if (!ValueToECMAInt32(f.cx, f.regs.sp[-2], &i) ||
        !ValueToECMAInt32(f.cx, f.regs.sp[-1], &j)) {
        THROW();
    }
    i = i ^ j;
    f.regs.sp[-2].setInt32(i);
}

void JS_FASTCALL
stubs::BitAnd(VMFrame &f)
{
    int32_t i, j;

    if (!ValueToECMAInt32(f.cx, f.regs.sp[-2], &i) ||
        !ValueToECMAInt32(f.cx, f.regs.sp[-1], &j)) {
        THROW();
    }
    i = i & j;
    f.regs.sp[-2].setInt32(i);
}

void JS_FASTCALL
stubs::Lsh(VMFrame &f)
{
    int32_t i, j;
    if (!ValueToECMAInt32(f.cx, f.regs.sp[-2], &i))
        THROW();
    if (!ValueToECMAInt32(f.cx, f.regs.sp[-1], &j))
        THROW();
    i = i << (j & 31);
    f.regs.sp[-2].setInt32(i);
}

void JS_FASTCALL
stubs::Rsh(VMFrame &f)
{
    int32_t i, j;
    if (!ValueToECMAInt32(f.cx, f.regs.sp[-2], &i))
        THROW();
    if (!ValueToECMAInt32(f.cx, f.regs.sp[-1], &j))
        THROW();
    i = i >> (j & 31);
    f.regs.sp[-2].setInt32(i);
}

template <int32 N>
static inline bool
PostInc(VMFrame &f, Value *vp)
{
    double d;
    if (!ValueToNumber(f.cx, *vp, &d))
        return false;
    f.regs.sp++;
    f.regs.sp[-1].setDouble(d);
    d += N;
    vp->setDouble(d);
    return true;
}

template <int32 N>
static inline bool
PreInc(VMFrame &f, Value *vp)
{
    double d;
    if (!ValueToNumber(f.cx, *vp, &d))
        return false;
    d += N;
    vp->setDouble(d);
    f.regs.sp++;
    f.regs.sp[-1].setDouble(d);
    return true;
}

void JS_FASTCALL
stubs::VpInc(VMFrame &f, Value *vp)
{
    if (!PostInc<1>(f, vp))
        THROW();
}

void JS_FASTCALL
stubs::VpDec(VMFrame &f, Value *vp)
{
    if (!PostInc<-1>(f, vp))
        THROW();
}

void JS_FASTCALL
stubs::DecVp(VMFrame &f, Value *vp)
{
    if (!PreInc<-1>(f, vp))
        THROW();
}

void JS_FASTCALL
stubs::IncVp(VMFrame &f, Value *vp)
{
    if (!PreInc<1>(f, vp))
        THROW();
}

static inline bool
DoInvoke(VMFrame &f, uint32 argc, Value *vp)
{
    JSContext *cx = f.cx;

    if (vp->isFunObj()) {
        JSObject *obj = &vp->asFunObj();
        JSFunction *fun = GET_FUNCTION_PRIVATE(cx, obj);

        JS_ASSERT(!FUN_INTERPRETED(fun));
        if (fun->flags & JSFUN_FAST_NATIVE) {
            JS_ASSERT(fun->u.n.extra == 0);
            JS_ASSERT(vp[1].isObjectOrNull() || PrimitiveValue::test(fun, vp[1]));
            JSBool ok = ((FastNative)fun->u.n.native)(cx, argc, vp);
            f.regs.sp = vp + 1;
            return ok ? true : false;
        }
    }

    JSBool ok = Invoke(cx, InvokeArgsGuard(vp, argc), 0);
    f.regs.sp = vp + 1;
    return ok ? true : false;
}

static inline bool
InlineCall(VMFrame &f, uint32 flags, void **pret, uint32 argc)
{
    JSContext *cx = f.cx;
    JSStackFrame *fp = f.fp;
    Value *vp = f.regs.sp - (argc + 2);
    JSObject *funobj = &vp->asFunObj();
    JSFunction *fun = GET_FUNCTION_PRIVATE(cx, funobj);

    JS_ASSERT(FUN_INTERPRETED(fun));

    JSScript *newscript = fun->u.i.script;

    if (f.inlineCallCount >= JS_MAX_INLINE_CALL_COUNT) {
        js_ReportOverRecursed(cx);
        return false;
    }

    /* Allocate the frame. */
    StackSpace &stack = cx->stack();
    uintN nslots = newscript->nslots;
    uintN funargs = fun->nargs;
    Value *argv = vp + 2;
    JSStackFrame *newfp;
    if (argc < funargs) {
        uintN missing = funargs - argc;
        newfp = stack.getInlineFrame(cx, f.regs.sp, missing, nslots);
        if (!newfp)
            return false;
        for (Value *v = argv + argc, *end = v + missing; v != end; ++v)
            v->setUndefined();
    } else {
        newfp = stack.getInlineFrame(cx, f.regs.sp, 0, nslots);
        if (!newfp)
            return false;
    }

    /* Initialize the frame. */
    newfp->ncode = NULL;
    newfp->callobj = NULL;
    newfp->argsobj = NULL;
    newfp->script = newscript;
    newfp->fun = fun;
    newfp->argc = argc;
    newfp->argv = vp + 2;
    newfp->rval.setUndefined();
    newfp->annotation = NULL;
    newfp->scopeChain = funobj->getParent();
    newfp->flags = flags;
    newfp->blockChain = NULL;
    JS_ASSERT(!JSFUN_BOUND_METHOD_TEST(fun->flags));
    newfp->thisv = vp[1];
    newfp->imacpc = NULL;

    /* Push void to initialize local variables. */
    Value *newslots = newfp->slots();
    Value *newsp = newslots + fun->u.i.nvars;
    for (Value *v = newslots; v != newsp; ++v)
        v->setUndefined();

    /* Scope with a call object parented by callee's parent. */
    if (fun->isHeavyweight() && !js_GetCallObject(cx, newfp))
        return false;

    /* :TODO: Switch version if currentVersion wasn't overridden. */
    newfp->callerVersion = (JSVersion)cx->version;

    // Marker for debug support.
    if (JSInterpreterHook hook = cx->debugHooks->callHook) {
        newfp->hookData = hook(cx, fp, JS_TRUE, 0,
                               cx->debugHooks->callHookData);
        // CHECK_INTERRUPT_HANDLER();
    } else {
        newfp->hookData = NULL;
    }

    f.inlineCallCount++;
    f.fp = newfp;
    stack.pushInlineFrame(cx, fp, cx->regs->pc, newfp);

    if (newscript->staticLevel < JS_DISPLAY_SIZE) {
        JSStackFrame **disp = &cx->display[newscript->staticLevel];
        newfp->displaySave = *disp;
        *disp = newfp;
    }

    f.regs.pc = newscript->code;
    f.regs.sp = newsp;

    if (cx->options & JSOPTION_METHODJIT) {
        if (!newscript->ncode) {
            if (mjit::TryCompile(cx, newscript, fun, newfp->scopeChain) == Compile_Error)
                return false;
        }
        JS_ASSERT(newscript->ncode);
        if (newscript->ncode != JS_UNJITTABLE_METHOD) {
            fp->ncode = f.scriptedReturn;
            *pret = newscript->ncode;
            return true;
        }
    }

    bool ok = !!Interpret(cx); //, newfp, f.inlineCallCount);
    stubs::Return(f);

    *pret = NULL;
    return ok;
}

void * JS_FASTCALL
stubs::Call(VMFrame &f, uint32 argc)
{
    Value *vp = f.regs.sp - (argc + 2);

    if (vp->isFunObj()) {
        JSObject *obj = &vp->asFunObj();
        JSFunction *fun = GET_FUNCTION_PRIVATE(cx, obj);
        if (FUN_INTERPRETED(fun)) {
            if (fun->u.i.script->isEmpty()) {
                vp->setUndefined();
                f.regs.sp = vp + 1;
                return NULL;
            }

            void *ret;
            if (!InlineCall(f, 0, &ret, argc))
                THROWV(NULL);

            f.cx->regs->pc = f.fp->script->code;

#ifdef JS_TRACER
            if (ret && f.cx->jitEnabled && IsTraceableRecursion(f.cx)) {
                /* Top of script should always have traceId 0. */
                f.u.tracer.traceId = 0;
                f.u.tracer.offs = 0;

                /* cx.regs.sp is only set in InlineCall() if non-jittable. */
                JS_ASSERT(f.cx->regs == &f.regs);

                /*
                 * NB: Normally, the function address is returned, and the
                 * caller's JIT'd code will set f.scriptedReturn and jump.
                 * Invoking the tracer breaks this in two ways:
                 *  1) f.scriptedReturn is not yet set, so when pushing new
                 *     inline frames, the call stack would get corrupted.
                 *  2) If the tracer does not push new frames, but runs some
                 *     code, the JIT'd code to set f.scriptedReturn will not
                 *     be run.
                 *
                 * So, a simple hack: set f.scriptedReturn now.
                 */
                f.scriptedReturn = GetReturnAddress(f, f.fp);

                void *newRet = InvokeTracer(f, Record_Recursion);

                /* 
                 * The tracer could have dropped us off anywhere. Hijack the
                 * stub return address to JaegerFromTracer, which will restore
                 * state correctly.
                 */
                if (newRet) {
                    void *ptr = JS_FUNC_TO_DATA_PTR(void *, JaegerFromTracer);
                    f.setReturnAddress(ReturnAddressPtr(FunctionPtr(ptr)));
                    return newRet;
                }
            }
#endif

            return ret;
        }
    }

    if (!DoInvoke(f, argc, vp))
        THROWV(NULL);

    return NULL;
}

void * JS_FASTCALL
stubs::New(VMFrame &f, uint32 argc)
{
    JSContext *cx = f.cx;
    Value *vp = f.regs.sp - (argc + 2);

    if (vp[0].isFunObj()) {
        JSObject *funobj = &vp[0].asFunObj();
        JSFunction *fun = GET_FUNCTION_PRIVATE(cx, funobj);
        if (fun->isInterpreted()) {
            jsid id = ATOM_TO_JSID(cx->runtime->atomState.classPrototypeAtom);
            if (!funobj->getProperty(cx, id, &vp[1]))
                THROWV(NULL);

            JSObject *proto = vp[1].isObject() ? &vp[1].asObject() : NULL;
            JSObject *obj2 = NewObject(cx, &js_ObjectClass, proto, funobj->getParent());
            if (!obj2)
                THROWV(NULL);

            if (fun->u.i.script->isEmpty()) {
                vp[0].setNonFunObj(*obj2);
                f.regs.sp = vp + 1;
                return NULL;
            }

            vp[1].setNonFunObj(*obj2);
            void *pret;
            if (!InlineCall(f, JSFRAME_CONSTRUCTING, &pret, argc))
                THROWV(NULL);
            return pret;
        }
    }

    if (!InvokeConstructor(cx, InvokeArgsGuard(vp, argc), JS_FALSE))
        THROWV(NULL);

    f.regs.sp = vp + 1;
    return NULL;
}

void JS_FASTCALL
stubs::DefFun(VMFrame &f, uint32 index)
{
    bool doSet;
    JSObject *pobj, *obj2;
    JSProperty *prop;
    uint32 old;

    JSContext *cx = f.cx;
    JSStackFrame *fp = f.fp;
    JSScript *script = fp->script;

    /*
     * A top-level function defined in Global or Eval code (see ECMA-262
     * Ed. 3), or else a SpiderMonkey extension: a named function statement in
     * a compound statement (not at the top statement level of global code, or
     * at the top level of a function body).
     */
    JSFunction *fun = script->getFunction(index);
    JSObject *obj = FUN_OBJECT(fun);

    if (FUN_NULL_CLOSURE(fun)) {
        /*
         * Even a null closure needs a parent for principals finding.
         * FIXME: bug 476950, although debugger users may also demand some kind
         * of scope link for debugger-assisted eval-in-frame.
         */
        obj2 = fp->scopeChain;
    } else {
        JS_ASSERT(!FUN_FLAT_CLOSURE(fun));

        /*
         * Inline js_GetScopeChain a bit to optimize for the case of a
         * top-level function.
         */
        if (!fp->blockChain) {
            obj2 = fp->scopeChain;
        } else {
            obj2 = js_GetScopeChain(cx, fp);
            if (!obj2)
                THROW();
        }
    }

    /*
     * If static link is not current scope, clone fun's object to link to the
     * current scope via parent. We do this to enable sharing of compiled
     * functions among multiple equivalent scopes, amortizing the cost of
     * compilation over a number of executions.  Examples include XUL scripts
     * and event handlers shared among Firefox or other Mozilla app chrome
     * windows, and user-defined JS functions precompiled and then shared among
     * requests in server-side JS.
     */
    if (obj->getParent() != obj2) {
        obj = CloneFunctionObject(cx, fun, obj2);
        if (!obj)
            THROW();
    }

    /*
     * Protect obj from any GC hiding below JSObject::setProperty or
     * JSObject::defineProperty.  All paths from here must flow through the
     * fp->scopeChain code below the parent->defineProperty call.
     */
    MUST_FLOW_THROUGH("restore_scope");
    fp->scopeChain = obj;

    Value rval;
    rval.setFunObj(*obj);

    /*
     * ECMA requires functions defined when entering Eval code to be
     * impermanent.
     */
    uintN attrs;
    attrs = (fp->flags & JSFRAME_EVAL)
            ? JSPROP_ENUMERATE
            : JSPROP_ENUMERATE | JSPROP_PERMANENT;

    /*
     * Load function flags that are also property attributes.  Getters and
     * setters do not need a slot, their value is stored elsewhere in the
     * property itself, not in obj slots.
     */
    PropertyOp getter, setter;
    uintN flags;
    
    getter = setter = PropertyStub;
    flags = JSFUN_GSFLAG2ATTR(fun->flags);
    if (flags) {
        /* Function cannot be both getter a setter. */
        JS_ASSERT(flags == JSPROP_GETTER || flags == JSPROP_SETTER);
        attrs |= flags | JSPROP_SHARED;
        rval.setUndefined();
        if (flags == JSPROP_GETTER)
            getter = CastAsPropertyOp(obj);
        else
            setter = CastAsPropertyOp(obj);
    }

    jsid id;
    JSBool ok;
    JSObject *parent;

    /*
     * We define the function as a property of the variable object and not the
     * current scope chain even for the case of function expression statements
     * and functions defined by eval inside let or with blocks.
     */
    parent = fp->varobj(cx);
    JS_ASSERT(parent);

    /*
     * Check for a const property of the same name -- or any kind of property
     * if executing with the strict option.  We check here at runtime as well
     * as at compile-time, to handle eval as well as multiple HTML script tags.
     */
    id = ATOM_TO_JSID(fun->atom);
    prop = NULL;
    ok = CheckRedeclaration(cx, parent, id, attrs, &pobj, &prop);
    if (!ok)
        goto restore_scope;

    /*
     * We deviate from 10.1.2 in ECMA 262 v3 and under eval use for function
     * declarations JSObject::setProperty, not JSObject::defineProperty, to
     * preserve the JSOP_PERMANENT attribute of existing properties and make
     * sure that such properties cannot be deleted.
     *
     * We also use JSObject::setProperty for the existing properties of Call
     * objects with matching attributes to preserve the native getters and
     * setters that store the value of the property in the interpreter frame,
     * see bug 467495.
     */
    doSet = (attrs == JSPROP_ENUMERATE);
    JS_ASSERT_IF(doSet, fp->flags & JSFRAME_EVAL);
    if (prop) {
        if (parent == pobj &&
            parent->getClass() == &js_CallClass &&
            (old = ((JSScopeProperty *) prop)->attributes(),
             !(old & (JSPROP_GETTER|JSPROP_SETTER)) &&
             (old & (JSPROP_ENUMERATE|JSPROP_PERMANENT)) == attrs)) {
            /*
             * js_CheckRedeclaration must reject attempts to add a getter or
             * setter to an existing property without a getter or setter.
             */
            JS_ASSERT(!(attrs & ~(JSPROP_ENUMERATE|JSPROP_PERMANENT)));
            JS_ASSERT(!(old & JSPROP_READONLY));
            doSet = JS_TRUE;
        }
        pobj->dropProperty(cx, prop);
    }
    ok = doSet
         ? parent->setProperty(cx, id, &rval)
         : parent->defineProperty(cx, id, rval, getter, setter, attrs);

  restore_scope:
    /* Restore fp->scopeChain now that obj is defined in fp->callobj. */
    fp->scopeChain = obj2;
    if (!ok)
        THROW();
}

#define DEFAULT_VALUE(cx, n, hint, v)                                         \
    JS_BEGIN_MACRO                                                            \
        JS_ASSERT(v.isObject());                                              \
        JS_ASSERT(v == regs.sp[n]);                                           \
        if (!v.asObject().defaultValue(cx, hint, &regs.sp[n]))                \
            THROWV(JS_FALSE);                                                 \
        v = regs.sp[n];                                                       \
    JS_END_MACRO

#define RELATIONAL(OP)                                                        \
    JS_BEGIN_MACRO                                                            \
        JSContext *cx = f.cx;                                                 \
        JSFrameRegs &regs = f.regs;                                           \
        Value rval = regs.sp[-1];                                             \
        Value lval = regs.sp[-2];                                             \
        bool cond;                                                            \
        if (lval.isObject())                                                  \
            DEFAULT_VALUE(cx, -2, JSTYPE_NUMBER, lval);                       \
        if (rval.isObject())                                                  \
            DEFAULT_VALUE(cx, -1, JSTYPE_NUMBER, rval);                       \
        if (BothString(lval, rval)) {                                         \
            JSString *l = lval.asString(), *r = rval.asString();              \
            cond = js_CompareStrings(l, r) OP 0;                              \
        } else {                                                              \
            double l, r;                                                      \
            if (!ValueToNumber(cx, lval, &l) ||                               \
                !ValueToNumber(cx, rval, &r)) {                               \
                THROWV(JS_FALSE);                                             \
            }                                                                 \
            cond = JSDOUBLE_COMPARE(l, OP, r, false);                         \
        }                                                                     \
        regs.sp[-2].setBoolean(cond);                                         \
        return cond;                                                          \
    JS_END_MACRO

JSBool JS_FASTCALL
stubs::LessThan(VMFrame &f)
{
    RELATIONAL(<);
}

JSBool JS_FASTCALL
stubs::LessEqual(VMFrame &f)
{
    RELATIONAL(<=);
}

JSBool JS_FASTCALL
stubs::GreaterThan(VMFrame &f)
{
    RELATIONAL(>);
}

JSBool JS_FASTCALL
stubs::GreaterEqual(VMFrame &f)
{
    RELATIONAL(>=);
}

JSBool JS_FASTCALL
stubs::ValueToBoolean(VMFrame &f)
{
    return js_ValueToBoolean(f.regs.sp[-1]);
}

/*
 * Inline copy of jsops.cpp:EQUALITY_OP().
 * @param op true if for JSOP_EQ; false for JSOP_NE.
 * @param ifnan return value upon NaN comparison.
 */
static inline bool
InlineEqualityOp(VMFrame &f, bool op, bool ifnan)
{
    Class *clasp;
    JSContext *cx = f.cx;
    JSFrameRegs &regs = f.regs;

    Value rval = regs.sp[-1];
    Value lval = regs.sp[-2];

    JSBool jscond;
    bool cond;

    #if JS_HAS_XML_SUPPORT
    /* Inline copy of jsops.cpp:XML_EQUALITY_OP() */
    if ((lval.isNonFunObj() && lval.asObject().isXML()) ||
        (rval.isNonFunObj() && rval.asObject().isXML())) {
        if (!js_TestXMLEquality(cx, lval, rval, &jscond))
            THROWV(false);
        cond = (jscond == JS_TRUE) == op;
    } else
    #endif /* JS_HAS_XML_SUPPORT */

    if (SamePrimitiveTypeOrBothObjects(lval, rval)) {
        if (lval.isString()) {
            JSString *l = lval.asString();
            JSString *r = rval.asString();
            cond = js_EqualStrings(l, r) == op;
        } else if (lval.isDouble()) {
            double l = lval.asDouble();
            double r = rval.asDouble();
            if (op) {
                cond = JSDOUBLE_COMPARE(l, ==, r, ifnan);
            } else {
                cond = JSDOUBLE_COMPARE(l, !=, r, ifnan);
            }
        } else {
            /* jsops.cpp:EXTENDED_EQUALITY_OP() */
            JSObject *lobj;
            if (lval.isObject() &&
                (lobj = &lval.asObject()) &&
                ((clasp = lobj->getClass())->flags & JSCLASS_IS_EXTENDED) &&
                ((ExtendedClass *)clasp)->equality) {
                if (!((ExtendedClass *)clasp)->equality(cx, lobj, lval, &jscond))
                    THROWV(false);
                cond = (jscond == JS_TRUE) == op;
            } else {
                cond = (lval.asRawUint32() == rval.asRawUint32()) == op;
            }
        }
    } else {
        if (lval.isNullOrUndefined()) {
            cond = rval.isNullOrUndefined() == op;
        } else if (rval.isNullOrUndefined()) {
            cond = !op;
        } else {
            if (lval.isObject()) {
                if (!lval.asObject().defaultValue(cx, JSTYPE_VOID, &regs.sp[-2]))
                    THROWV(false);
                lval = regs.sp[-2];
            }

            if (rval.isObject()) {
                if (!rval.asObject().defaultValue(cx, JSTYPE_VOID, &regs.sp[-1]))
                    THROWV(false);
                rval = regs.sp[-1];
            }

            if (BothString(lval, rval)) {
                JSString *l = lval.asString();
                JSString *r = rval.asString();
                cond = js_EqualStrings(l, r) == op;
            } else {
                double l, r;
                if (!ValueToNumber(cx, lval, &l) ||
                    !ValueToNumber(cx, rval, &r)) {
                    THROWV(false);
                }
                
                if (op)
                    cond = JSDOUBLE_COMPARE(l, ==, r, ifnan);
                else
                    cond = JSDOUBLE_COMPARE(l, !=, r, ifnan);
            }
        }
    }

    regs.sp[-2].setBoolean(cond);
    return cond;
}

JSBool JS_FASTCALL
stubs::Equal(VMFrame &f)
{
    return InlineEqualityOp(f, true, false);
}

JSBool JS_FASTCALL
stubs::NotEqual(VMFrame &f)
{
    return InlineEqualityOp(f, false, true);
}

static inline bool
DefaultValue(VMFrame &f, JSType hint, Value &v, int n)
{
    JS_ASSERT(v.isObject());
    if (!v.asObject().defaultValue(f.cx, hint, &f.regs.sp[n]))
        return false;
    v = f.regs.sp[n];
    return true;
}

void JS_FASTCALL
stubs::Add(VMFrame &f)
{
    JSContext *cx = f.cx;
    JSFrameRegs &regs = f.regs;
    Value rval = regs.sp[-1];
    Value lval = regs.sp[-2];

    if (BothInt32(lval, rval)) {
        int32_t l = lval.asInt32(), r = rval.asInt32();
        int32_t sum = l + r;
        regs.sp--;
        if (JS_UNLIKELY(bool((l ^ sum) & (r ^ sum) & 0x80000000)))
            regs.sp[-1].setDouble(double(l) + double(r));
        else
            regs.sp[-1].setInt32(sum);
    } else
#if JS_HAS_XML_SUPPORT
    if (lval.isNonFunObj() && lval.asObject().isXML() &&
        rval.isNonFunObj() && rval.asObject().isXML()) {
        if (!js_ConcatenateXML(cx, &lval.asObject(), &rval.asObject(), &rval))
            THROW();
        regs.sp--;
        regs.sp[-1] = rval;
    } else
#endif
    {
        if (lval.isObject() && !DefaultValue(f, JSTYPE_VOID, lval, -2))
            THROW();
        if (rval.isObject() && !DefaultValue(f, JSTYPE_VOID, rval, -1))
            THROW();
        bool lIsString, rIsString;
        if ((lIsString = lval.isString()) | (rIsString = rval.isString())) {
            JSString *lstr, *rstr;
            if (lIsString) {
                lstr = lval.asString();
            } else {
                lstr = js_ValueToString(cx, lval);
                if (!lstr)
                    THROW();
                regs.sp[-2].setString(lstr);
            }
            if (rIsString) {
                rstr = rval.asString();
            } else {
                rstr = js_ValueToString(cx, rval);
                if (!rstr)
                    THROW();
                regs.sp[-1].setString(rstr);
            }
            JSString *str = js_ConcatStrings(cx, lstr, rstr);
            if (!str)
                THROW();
            regs.sp--;
            regs.sp[-1].setString(str);
        } else {
            double l, r;
            if (!ValueToNumber(cx, lval, &l) || !ValueToNumber(cx, rval, &r))
                THROW();
            l += r;
            regs.sp--;
            regs.sp[-1].setNumber(l);
        }
    }
}


void JS_FASTCALL
stubs::Sub(VMFrame &f)
{
    JSContext *cx = f.cx;
    JSFrameRegs &regs = f.regs;
    double d1, d2;
    if (!ValueToNumber(cx, regs.sp[-2], &d1) ||
        !ValueToNumber(cx, regs.sp[-1], &d2)) {
        THROW();
    }
    double d = d1 - d2;
    regs.sp[-2].setNumber(d);
}

void JS_FASTCALL
stubs::Mul(VMFrame &f)
{
    JSContext *cx = f.cx;
    JSFrameRegs &regs = f.regs;
    double d1, d2;
    if (!ValueToNumber(cx, regs.sp[-2], &d1) ||
        !ValueToNumber(cx, regs.sp[-1], &d2)) {
        THROW();
    }
    double d = d1 * d2;
    regs.sp[-2].setNumber(d);
}

void JS_FASTCALL
stubs::Div(VMFrame &f)
{
    JSContext *cx = f.cx;
    JSRuntime *rt = cx->runtime;
    JSFrameRegs &regs = f.regs;

    double d1, d2;
    if (!ValueToNumber(cx, regs.sp[-2], &d1) ||
        !ValueToNumber(cx, regs.sp[-1], &d2)) {
        THROW();
    }
    if (d2 == 0) {
        const Value *vp;
#ifdef XP_WIN
        /* XXX MSVC miscompiles such that (NaN == 0) */
        if (JSDOUBLE_IS_NaN(d2))
            vp = &rt->NaNValue;
        else
#endif
        if (d1 == 0 || JSDOUBLE_IS_NaN(d1))
            vp = &rt->NaNValue;
        else if (JSDOUBLE_IS_NEG(d1) != JSDOUBLE_IS_NEG(d2))
            vp = &rt->negativeInfinityValue;
        else
            vp = &rt->positiveInfinityValue;
        regs.sp[-2] = *vp;
    } else {
        d1 /= d2;
        regs.sp[-2].setNumber(d1);
    }
}

void JS_FASTCALL
stubs::Mod(VMFrame &f)
{
    JSContext *cx = f.cx;
    JSFrameRegs &regs = f.regs;

    Value &lref = regs.sp[-2];
    Value &rref = regs.sp[-1];
    int32_t l, r;
    if (lref.isInt32() && rref.isInt32() &&
        (l = lref.asInt32()) >= 0 && (r = rref.asInt32()) > 0) {
        int32_t mod = l % r;
        regs.sp[-2].setInt32(mod);
    } else {
        double d1, d2;
        if (!ValueToNumber(cx, regs.sp[-2], &d1) ||
            !ValueToNumber(cx, regs.sp[-1], &d2)) {
            THROW();
        }
        if (d2 == 0) {
            regs.sp[-2].setDouble(js_NaN);
        } else {
            d1 = js_fmod(d1, d2);
            regs.sp[-2].setDouble(d1);
        }
    }
}

JSObject *JS_FASTCALL
stubs::NewArray(VMFrame &f, uint32 len)
{
    JSObject *obj = js_NewArrayObject(f.cx, len, f.regs.sp - len, JS_TRUE);
    if (!obj)
        THROWV(NULL);
    return obj;
}

void JS_FASTCALL
stubs::This(VMFrame &f)
{
    if (!f.fp->getThisObject(f.cx))
        THROW();
    f.regs.sp[0] = f.fp->thisv;
}

void JS_FASTCALL
stubs::Neg(VMFrame &f)
{
    double d;
    if (!ValueToNumber(f.cx, f.regs.sp[-1], &d))
        THROW();
    d = -d;
    f.regs.sp[-1].setNumber(d);
}

void JS_FASTCALL
stubs::ObjToStr(VMFrame &f)
{
    const Value &ref = f.regs.sp[-1];
    if (ref.isObject()) {
        JSString *str = js_ValueToString(f.cx, ref);
        if (!str)
            THROW();
        f.regs.sp[-1].setString(str);
    }
}

JSObject * JS_FASTCALL
stubs::NewInitArray(VMFrame &f)
{
    JSObject *obj = js_NewArrayObject(f.cx, 0, NULL);
    if (!obj)
        THROWV(NULL);
    return obj;
}

JSObject * JS_FASTCALL
stubs::NewInitObject(VMFrame &f, uint32 empty)
{
    JSContext *cx = f.cx;

    JSObject *obj = NewObject(cx, &js_ObjectClass, NULL, NULL);
    if (!obj)
        THROWV(NULL);

    if (!empty) {
        JS_LOCK_OBJ(cx, obj);
        JSScope *scope = js_GetMutableScope(cx, obj);
        if (!scope) {
            JS_UNLOCK_OBJ(cx, obj);
            THROWV(NULL);
        }

        /*
         * We cannot assume that js_GetMutableScope above creates a scope
         * owned by cx and skip JS_UNLOCK_SCOPE. A new object debugger
         * hook may add properties to the newly created object, suspend
         * the current request and share the object with other threads.
         */
        JS_UNLOCK_SCOPE(cx, scope);
    }

    return obj;
}

void JS_FASTCALL
stubs::EndInit(VMFrame &f)
{
    JS_ASSERT(f.regs.sp - f.fp->base() >= 1);
    const Value &lref = f.regs.sp[-1];
    JS_ASSERT(lref.isObject());
    f.cx->weakRoots.finalizableNewborns[FINALIZE_OBJECT] = &lref.asObject();
}

void JS_FASTCALL
stubs::InitElem(VMFrame &f, uint32 last)
{
    JSContext *cx = f.cx;
    JSFrameRegs &regs = f.regs;

    /* Pop the element's value into rval. */
    JS_ASSERT(regs.sp - f.fp->base() >= 3);
    const Value &rref = regs.sp[-1];

    /* Find the object being initialized at top of stack. */
    const Value &lref = regs.sp[-3];
    JS_ASSERT(lref.isObject());
    JSObject *obj = &lref.asObject();

    /* Fetch id now that we have obj. */
    jsid id;
    const Value &idval = regs.sp[-2];
    if (!FetchElementId(f, obj, idval, id, &regs.sp[-2]))
        THROW();

    /*
     * Check for property redeclaration strict warning (we may be in an object
     * initialiser, not an array initialiser).
     */
    if (!CheckRedeclaration(cx, obj, id, JSPROP_INITIALIZER, NULL, NULL))
        THROW();

    /*
     * If rref is a hole, do not call JSObject::defineProperty. In this case,
     * obj must be an array, so if the current op is the last element
     * initialiser, set the array length to one greater than id.
     */
    if (rref.isMagic(JS_ARRAY_HOLE)) {
        JS_ASSERT(obj->isArray());
        JS_ASSERT(JSID_IS_INT(id));
        JS_ASSERT(jsuint(JSID_TO_INT(id)) < JS_ARGS_LENGTH_MAX);
        if (last && !js_SetLengthProperty(cx, obj, (jsuint) (JSID_TO_INT(id) + 1)))
            THROW();
    } else {
        if (!obj->defineProperty(cx, id, rref, NULL, NULL, JSPROP_ENUMERATE))
            THROW();
    }
}

