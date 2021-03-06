/* ***** BEGIN LICENSE BLOCK *****
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
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * Red Hat, Inc.
 * Portions created by the Initial Developer are Copyright (C) 2006
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Kai Engert <kengert@redhat.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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

#ifndef _NSPSMBACKGROUNDTHREAD_H_
#define _NSPSMBACKGROUNDTHREAD_H_

#include "nspr.h"
#include "nscore.h"
#include "mozilla/CondVar.h"
#include "mozilla/Mutex.h"
#include "nsNSSComponent.h"

class nsPSMBackgroundThread
{
protected:
  static void PR_CALLBACK nsThreadRunner(void *arg);
  virtual void Run(void) = 0;

  // used to join the thread
  PRThread *mThreadHandle;

  // Shared mutex used for condition variables,
  // and to protect access to mExitState.
  // Derived classes may use it to protect additional
  // resources.
  mozilla::Mutex mMutex;

  // Used to signal the thread's Run loop when a job is added 
  // and/or exit is requested.
  mozilla::CondVar mCond;

  PRBool exitRequested(::mozilla::MutexAutoLock const & proofOfLock) const;
  PRBool exitRequestedNoLock() const { return mExitState != ePSMThreadRunning; }
  nsresult postStoppedEventToMainThread(::mozilla::MutexAutoLock const & proofOfLock);

private:
  enum {
    ePSMThreadRunning = 0,
    ePSMThreadStopRequested = 1,
    ePSMThreadStopped = 2
  } mExitState;

public:
  nsPSMBackgroundThread();
  virtual ~nsPSMBackgroundThread();

  nsresult startThread();
  void requestExit();
};


#endif
