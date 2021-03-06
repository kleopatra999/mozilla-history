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
 *  The Mozilla Foundation
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Michal Novotny <michal.novotny@gmail.com>
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

#include "nsWyciwyg.h"

#include "mozilla/net/NeckoChild.h"
#include "WyciwygChannelChild.h"

#include "nsIParser.h"
#include "nsStringStream.h"
#include "nsMimeTypes.h"
#include "nsNetUtil.h"
#include "nsISerializable.h"
#include "nsSerializationHelper.h"

namespace mozilla {
namespace net {

NS_IMPL_ISUPPORTS3(WyciwygChannelChild,
                   nsIRequest,
                   nsIChannel,
                   nsIWyciwygChannel);


WyciwygChannelChild::WyciwygChannelChild()
  : mStatus(NS_OK)
  , mIsPending(PR_FALSE)
  , mCanceled(false)
  , mLoadFlags(LOAD_NORMAL)
  , mContentLength(-1)
  , mCharsetSource(kCharsetUninitialized)
  , mState(WCC_NEW)
  , mIPCOpen(false)
  , mEventQ(this)
{
  LOG(("Creating WyciwygChannelChild @%x\n", this));
}

WyciwygChannelChild::~WyciwygChannelChild()
{
  LOG(("Destroying WyciwygChannelChild @%x\n", this));
}

void
WyciwygChannelChild::AddIPDLReference()
{
  NS_ABORT_IF_FALSE(!mIPCOpen, "Attempt to retain more than one IPDL reference");
  mIPCOpen = true;
  AddRef();
}

void
WyciwygChannelChild::ReleaseIPDLReference()
{
  NS_ABORT_IF_FALSE(mIPCOpen, "Attempt to release nonexistent IPDL reference");
  mIPCOpen = false;
  Release();
}

nsresult
WyciwygChannelChild::Init(nsIURI* uri)
{
  NS_ENSURE_ARG_POINTER(uri);

  mState = WCC_INIT;

  mURI = uri;
  mOriginalURI = uri;

  SendInit(IPC::URI(mURI));
  return NS_OK;
}

//-----------------------------------------------------------------------------
// WyciwygChannelChild::PWyciwygChannelChild
//-----------------------------------------------------------------------------

class WyciwygStartRequestEvent : public ChannelEvent
{
public:
  WyciwygStartRequestEvent(WyciwygChannelChild* child,
                           const nsresult& statusCode,
                           const PRInt32& contentLength,
                           const PRInt32& source,
                           const nsCString& charset,
                           const nsCString& securityInfo)
  : mChild(child), mStatusCode(statusCode), mContentLength(contentLength),
    mSource(source), mCharset(charset), mSecurityInfo(securityInfo) {}
  void Run() { mChild->OnStartRequest(mStatusCode, mContentLength, mSource,
                                     mCharset, mSecurityInfo); }
private:
  WyciwygChannelChild* mChild;
  nsresult mStatusCode;
  PRInt32 mContentLength;
  PRInt32 mSource;
  nsCString mCharset;
  nsCString mSecurityInfo;
};

bool
WyciwygChannelChild::RecvOnStartRequest(const nsresult& statusCode,
                                        const PRInt32& contentLength,
                                        const PRInt32& source,
                                        const nsCString& charset,
                                        const nsCString& securityInfo)
{
  if (mEventQ.ShouldEnqueue()) {
    mEventQ.Enqueue(new WyciwygStartRequestEvent(this, statusCode,
                                                 contentLength, source,
                                                 charset, securityInfo));
  } else {
    OnStartRequest(statusCode, contentLength, source, charset, securityInfo);
  }
  return true;
}

void
WyciwygChannelChild::OnStartRequest(const nsresult& statusCode,
                                    const PRInt32& contentLength,
                                    const PRInt32& source,
                                    const nsCString& charset,
                                    const nsCString& securityInfo)
{
  LOG(("WyciwygChannelChild::RecvOnStartRequest [this=%x]\n", this));

  mState = WCC_ONSTART;

  mStatus = statusCode;
  mContentLength = contentLength;
  mCharsetSource = source;
  mCharset = charset;

  if (!securityInfo.IsEmpty()) {
    NS_DeserializeObject(securityInfo, getter_AddRefs(mSecurityInfo));
  }

  AutoEventEnqueuer ensureSerialDispatch(mEventQ);

  nsresult rv = mListener->OnStartRequest(this, mListenerContext);
  if (NS_FAILED(rv))
    Cancel(rv);
}

class WyciwygDataAvailableEvent : public ChannelEvent
{
public:
  WyciwygDataAvailableEvent(WyciwygChannelChild* child,
                            const nsCString& data,
                            const PRUint32& offset)
  : mChild(child), mData(data), mOffset(offset) {}
  void Run() { mChild->OnDataAvailable(mData, mOffset); }
private:
  WyciwygChannelChild* mChild;
  nsCString mData;
  PRUint32 mOffset;
};

bool
WyciwygChannelChild::RecvOnDataAvailable(const nsCString& data,
                                         const PRUint32& offset)
{
  if (mEventQ.ShouldEnqueue()) {
    mEventQ.Enqueue(new WyciwygDataAvailableEvent(this, data, offset));
  } else {
    OnDataAvailable(data, offset);
  }
  return true;
}

void
WyciwygChannelChild::OnDataAvailable(const nsCString& data,
                                     const PRUint32& offset)
{
  LOG(("WyciwygChannelChild::RecvOnDataAvailable [this=%x]\n", this));

  if (mCanceled)
    return;

  mState = WCC_ONDATA;

  // NOTE: the OnDataAvailable contract requires the client to read all the data
  // in the inputstream.  This code relies on that ('data' will go away after
  // this function).  Apparently the previous, non-e10s behavior was to actually
  // support only reading part of the data, allowing later calls to read the
  // rest.
  nsCOMPtr<nsIInputStream> stringStream;
  nsresult rv = NS_NewByteInputStream(getter_AddRefs(stringStream),
                                      data.get(),
                                      data.Length(),
                                      NS_ASSIGNMENT_DEPEND);
  if (NS_FAILED(rv)) {
    Cancel(rv);
    return;
  }

  AutoEventEnqueuer ensureSerialDispatch(mEventQ);
  
  rv = mListener->OnDataAvailable(this, mListenerContext,
                                  stringStream, offset, data.Length());
  if (NS_FAILED(rv))
    Cancel(rv);

  if (mProgressSink && NS_SUCCEEDED(rv) && !(mLoadFlags & LOAD_BACKGROUND))
    mProgressSink->OnProgress(this, nsnull, PRUint64(offset + data.Length()),
                              PRUint64(mContentLength));
}

class WyciwygStopRequestEvent : public ChannelEvent
{
public:
  WyciwygStopRequestEvent(WyciwygChannelChild* child,
                          const nsresult& statusCode)
  : mChild(child), mStatusCode(statusCode) {}
  void Run() { mChild->OnStopRequest(mStatusCode); }
private:
  WyciwygChannelChild* mChild;
  nsresult mStatusCode;
};

bool
WyciwygChannelChild::RecvOnStopRequest(const nsresult& statusCode)
{
  if (mEventQ.ShouldEnqueue()) {
    mEventQ.Enqueue(new WyciwygStopRequestEvent(this, statusCode));
  } else {
    OnStopRequest(statusCode);
  }
  return true;
}

void
WyciwygChannelChild::OnStopRequest(const nsresult& statusCode)
{
  LOG(("WyciwygChannelChild::RecvOnStopRequest [this=%x status=%u]\n",
           this, statusCode));

  { // We need to ensure that all IPDL message dispatching occurs
    // before we delete the protocol below
    AutoEventEnqueuer ensureSerialDispatch(mEventQ);

    mState = WCC_ONSTOP;

    mIsPending = PR_FALSE;

    if (!mCanceled)
      mStatus = statusCode;

    mListener->OnStopRequest(this, mListenerContext, statusCode);

    mListener = 0;
    mListenerContext = 0;

    if (mLoadGroup)
      mLoadGroup->RemoveRequest(this, nsnull, mStatus);

    mCallbacks = 0;
    mProgressSink = 0;
  }

  if (mIPCOpen)
    PWyciwygChannelChild::Send__delete__(this);
}

class WyciwygCancelEvent : public ChannelEvent
{
 public:
  WyciwygCancelEvent(WyciwygChannelChild* child, const nsresult& status)
  : mChild(child)
  , mStatus(status) {}

  void Run() { mChild->CancelEarly(mStatus); }
 private:
  WyciwygChannelChild* mChild;
  nsresult mStatus;
};

bool
WyciwygChannelChild::RecvCancelEarly(const nsresult& statusCode)
{
  if (mEventQ.ShouldEnqueue()) {
    mEventQ.Enqueue(new WyciwygCancelEvent(this, statusCode));
  } else {
    CancelEarly(statusCode);
  }
  return true;
}

void WyciwygChannelChild::CancelEarly(const nsresult& statusCode)
{
  LOG(("WyciwygChannelChild::CancelEarly [this=%x]\n", this));
  
  if (mCanceled)
    return;

  mCanceled = true;
  mStatus = statusCode;
  
  mIsPending = false;
  if (mLoadGroup)
    mLoadGroup->RemoveRequest(this, nsnull, mStatus);

  if (mListener) {
    mListener->OnStartRequest(this, mListenerContext);
    mListener->OnStopRequest(this, mListenerContext, mStatus);
  }
  mListener = nsnull;
  mListenerContext = nsnull;

  if (mIPCOpen)
    PWyciwygChannelChild::Send__delete__(this);
}

//-----------------------------------------------------------------------------
// nsIRequest
//-----------------------------------------------------------------------------

/* readonly attribute AUTF8String name; */
NS_IMETHODIMP
WyciwygChannelChild::GetName(nsACString & aName)
{
  return mURI->GetSpec(aName);
}

/* boolean isPending (); */
NS_IMETHODIMP
WyciwygChannelChild::IsPending(PRBool *aIsPending)
{
  *aIsPending = mIsPending;
  return NS_OK;
}

/* readonly attribute nsresult status; */
NS_IMETHODIMP
WyciwygChannelChild::GetStatus(nsresult *aStatus)
{
  *aStatus = mStatus;
  return NS_OK;
}

/* void cancel (in nsresult aStatus); */
NS_IMETHODIMP
WyciwygChannelChild::Cancel(nsresult aStatus)
{
  if (mCanceled)
    return NS_OK;

  mCanceled = true;
  mStatus = aStatus;
  if (mIPCOpen)
    SendCancel(aStatus);
  return NS_OK;
}

/* void suspend (); */
NS_IMETHODIMP
WyciwygChannelChild::Suspend()
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

/* void resume (); */
NS_IMETHODIMP
WyciwygChannelChild::Resume()
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

/* attribute nsILoadGroup loadGroup; */
NS_IMETHODIMP
WyciwygChannelChild::GetLoadGroup(nsILoadGroup * *aLoadGroup)
{
  *aLoadGroup = mLoadGroup;
  NS_IF_ADDREF(*aLoadGroup);
  return NS_OK;
}
NS_IMETHODIMP
WyciwygChannelChild::SetLoadGroup(nsILoadGroup * aLoadGroup)
{
  mLoadGroup = aLoadGroup;
  NS_QueryNotificationCallbacks(mCallbacks,
                                mLoadGroup,
                                NS_GET_IID(nsIProgressEventSink),
                                getter_AddRefs(mProgressSink));
  return NS_OK;
}

/* attribute nsLoadFlags loadFlags; */
NS_IMETHODIMP
WyciwygChannelChild::GetLoadFlags(nsLoadFlags *aLoadFlags)
{
  *aLoadFlags = mLoadFlags;
  return NS_OK;
}
NS_IMETHODIMP
WyciwygChannelChild::SetLoadFlags(nsLoadFlags aLoadFlags)
{
  mLoadFlags = aLoadFlags;
  return NS_OK;
}


//-----------------------------------------------------------------------------
// nsIChannel
//-----------------------------------------------------------------------------

/* attribute nsIURI originalURI; */
NS_IMETHODIMP
WyciwygChannelChild::GetOriginalURI(nsIURI * *aOriginalURI)
{
  *aOriginalURI = mOriginalURI;
  NS_ADDREF(*aOriginalURI);
  return NS_OK;
}
NS_IMETHODIMP
WyciwygChannelChild::SetOriginalURI(nsIURI * aOriginalURI)
{
  NS_ENSURE_TRUE(mState == WCC_INIT, NS_ERROR_UNEXPECTED);

  NS_ENSURE_ARG_POINTER(aOriginalURI);
  mOriginalURI = aOriginalURI;
  return NS_OK;
}

/* readonly attribute nsIURI URI; */
NS_IMETHODIMP
WyciwygChannelChild::GetURI(nsIURI * *aURI)
{
  *aURI = mURI;
  NS_IF_ADDREF(*aURI);
  return NS_OK;
}

/* attribute nsISupports owner; */
NS_IMETHODIMP
WyciwygChannelChild::GetOwner(nsISupports * *aOwner)
{
  NS_PRECONDITION(mOwner, "Must have a principal!");
  NS_ENSURE_STATE(mOwner);

  NS_ADDREF(*aOwner = mOwner);
  return NS_OK;
}
NS_IMETHODIMP
WyciwygChannelChild::SetOwner(nsISupports * aOwner)
{
  mOwner = aOwner;
  return NS_OK;
}

/* attribute nsIInterfaceRequestor notificationCallbacks; */
NS_IMETHODIMP
WyciwygChannelChild::GetNotificationCallbacks(nsIInterfaceRequestor * *aCallbacks)
{
  *aCallbacks = mCallbacks;
  NS_IF_ADDREF(*aCallbacks);
  return NS_OK;
}
NS_IMETHODIMP
WyciwygChannelChild::SetNotificationCallbacks(nsIInterfaceRequestor * aCallbacks)
{
  mCallbacks = aCallbacks;
  NS_QueryNotificationCallbacks(mCallbacks,
                                mLoadGroup,
                                NS_GET_IID(nsIProgressEventSink),
                                getter_AddRefs(mProgressSink));
  return NS_OK;
}

/* readonly attribute nsISupports securityInfo; */
NS_IMETHODIMP
WyciwygChannelChild::GetSecurityInfo(nsISupports * *aSecurityInfo)
{
  NS_IF_ADDREF(*aSecurityInfo = mSecurityInfo);

  return NS_OK;
}

/* attribute ACString contentType; */
NS_IMETHODIMP
WyciwygChannelChild::GetContentType(nsACString & aContentType)
{
  aContentType.AssignLiteral(WYCIWYG_TYPE);
  return NS_OK;
}
NS_IMETHODIMP
WyciwygChannelChild::SetContentType(const nsACString & aContentType)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

/* attribute ACString contentCharset; */
NS_IMETHODIMP
WyciwygChannelChild::GetContentCharset(nsACString & aContentCharset)
{
  aContentCharset.Assign("UTF-16");
  return NS_OK;
}
NS_IMETHODIMP
WyciwygChannelChild::SetContentCharset(const nsACString & aContentCharset)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

/* attribute long contentLength; */
NS_IMETHODIMP
WyciwygChannelChild::GetContentLength(PRInt32 *aContentLength)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}
NS_IMETHODIMP
WyciwygChannelChild::SetContentLength(PRInt32 aContentLength)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

/* nsIInputStream open (); */
NS_IMETHODIMP
WyciwygChannelChild::Open(nsIInputStream **_retval)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

/* void asyncOpen (in nsIStreamListener aListener, in nsISupports aContext); */
NS_IMETHODIMP
WyciwygChannelChild::AsyncOpen(nsIStreamListener *aListener, nsISupports *aContext)
{
  LOG(("WyciwygChannelChild::AsyncOpen [this=%x]\n", this));

  // The only places creating wyciwyg: channels should be
  // HTMLDocument::OpenCommon and session history.  Both should be setting an
  // owner.
  NS_PRECONDITION(mOwner, "Must have a principal");
  NS_ENSURE_STATE(mOwner);

  NS_ENSURE_ARG_POINTER(aListener);
  NS_ENSURE_TRUE(!mIsPending, NS_ERROR_IN_PROGRESS);

  mListener = aListener;
  mListenerContext = aContext;
  mIsPending = PR_TRUE;

  if (mLoadGroup)
    mLoadGroup->AddRequest(this, nsnull);

  SendAsyncOpen(IPC::URI(mOriginalURI), mLoadFlags);

  mState = WCC_OPENED;

  return NS_OK;
}


//-----------------------------------------------------------------------------
// nsIWyciwygChannel
//-----------------------------------------------------------------------------

/* void writeToCacheEntry (in AString aData); */
NS_IMETHODIMP
WyciwygChannelChild::WriteToCacheEntry(const nsAString & aData)
{
  NS_ENSURE_TRUE((mState == WCC_INIT) ||
                 (mState == WCC_ONWRITE), NS_ERROR_UNEXPECTED);

  SendWriteToCacheEntry(PromiseFlatString(aData));
  mState = WCC_ONWRITE;
  return NS_OK;
}

/* void closeCacheEntry (in nsresult reason); */
NS_IMETHODIMP
WyciwygChannelChild::CloseCacheEntry(nsresult reason)
{
  NS_ENSURE_TRUE(mState == WCC_ONWRITE, NS_ERROR_UNEXPECTED);

  SendCloseCacheEntry(reason);
  mState = WCC_ONCLOSED;

  if (mIPCOpen)
    PWyciwygChannelChild::Send__delete__(this);

  return NS_OK;
}

/* void setSecurityInfo (in nsISupports aSecurityInfo); */
NS_IMETHODIMP
WyciwygChannelChild::SetSecurityInfo(nsISupports *aSecurityInfo)
{
  mSecurityInfo = aSecurityInfo;

  if (mSecurityInfo) {
    nsCOMPtr<nsISerializable> serializable = do_QueryInterface(mSecurityInfo);
    if (serializable) {
      nsCString secInfoStr;
      NS_SerializeToString(serializable, secInfoStr);
      SendSetSecurityInfo(secInfoStr);
    }
    else {
      NS_WARNING("Can't serialize security info");
    }
  }

  return NS_OK;
}

/* void setCharsetAndSource (in long aSource, in ACString aCharset); */
NS_IMETHODIMP
WyciwygChannelChild::SetCharsetAndSource(PRInt32 aSource, const nsACString & aCharset)
{
  // mState == WCC_ONSTART when reading from the channel
  // mState == WCC_INIT when writing to the cache
  NS_ENSURE_TRUE((mState == WCC_ONSTART) ||
                 (mState == WCC_INIT), NS_ERROR_UNEXPECTED);

  mCharsetSource = aSource;
  mCharset = aCharset;

  // TODO ensure that nsWyciwygChannel in the parent has still the cache entry
  SendSetCharsetAndSource(mCharsetSource, mCharset);
  return NS_OK;
}

/* ACString getCharsetAndSource (out long aSource); */
NS_IMETHODIMP
WyciwygChannelChild::GetCharsetAndSource(PRInt32 *aSource NS_OUTPARAM, nsACString & _retval)
{
  NS_ENSURE_TRUE((mState == WCC_ONSTART) ||
                 (mState == WCC_ONDATA) ||
                 (mState == WCC_ONSTOP), NS_ERROR_NOT_AVAILABLE);

  if (mCharsetSource == kCharsetUninitialized)
    return NS_ERROR_NOT_AVAILABLE;

  *aSource = mCharsetSource;
  _retval = mCharset;
  return NS_OK;
}

//------------------------------------------------------------------------------
}} // mozilla::net
