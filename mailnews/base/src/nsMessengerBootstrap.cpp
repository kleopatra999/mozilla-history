/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * The contents of this file are subject to the Netscape Public
 * License Version 1.1 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy of
 * the License at http://www.mozilla.org/NPL/
 *
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 *
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is Netscape
 * Communications Corporation.  Portions created by Netscape are
 * Copyright (C) 1998 Netscape Communications Corporation. All
 * Rights Reserved.
 *
 * Contributor(s): 
 */

#include "nsMessengerBootstrap.h"
#include "nsCOMPtr.h"

#include "nsDOMCID.h"
#include "nsMsgBaseCID.h"
#include "nsIMsgMailSession.h"
#include "nsIMsgFolderCache.h"

static NS_DEFINE_CID(kCMsgMailSessionCID, NS_MSGMAILSESSION_CID); 
static NS_DEFINE_CID(kMsgAccountManagerCID, NS_MSGACCOUNTMANAGER_CID);


NS_IMPL_ISUPPORTS(nsMessengerBootstrap, nsCOMTypeInfo<nsIAppShellComponent>::GetIID())

nsMessengerBootstrap::nsMessengerBootstrap()
{
  NS_INIT_REFCNT();
  
}

nsMessengerBootstrap::~nsMessengerBootstrap()
{
}

nsresult
nsMessengerBootstrap::Initialize(nsIAppShellService*,
                                 nsICmdLineService*)
{
	nsresult rv;
    rv = nsServiceManager::RegisterService( "component://netscape/appshell/component/messenger", this );

	return rv;
}

nsresult
nsMessengerBootstrap::Shutdown()
{
	nsresult finalrv = NS_OK;
	nsresult rv;

	NS_WITH_SERVICE(nsIMsgAccountManager, accountManager, kMsgAccountManagerCID, &rv);
	if (NS_SUCCEEDED(rv) && accountManager)
	{
		// we should really move the folder cache completely to
		// the account manager, but I'll wait for m12.
		nsCOMPtr <nsIMsgFolderCache> folderCache;
		accountManager->GetFolderCache(getter_AddRefs(folderCache));
		if (folderCache)
			accountManager->WriteToFolderCache(folderCache);
		accountManager->CloseCachedConnections();
		accountManager->UnloadAccounts();
	}
	rv = nsServiceManager::UnregisterService("component://netscape/appshell/component/messenger");
	if(NS_FAILED(rv)) finalrv = rv;

	rv = nsServiceManager::UnregisterService("component://netscape/messenger/services/session");
	if(NS_FAILED(rv)) finalrv = rv;

	rv = nsServiceManager::UnregisterService("component://netscape/messenger/biffManager");
	if(NS_FAILED(rv)) finalrv = rv;

	return finalrv;
}


