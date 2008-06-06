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
 * The Original Code is the Netscape security libraries.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1994-2000
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
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

#include "sslsample.h"
#include "sslerror.h"

/* Declare SSL cipher suites. */

int ssl2CipherSuites[] = {
	SSL_EN_RC4_128_WITH_MD5,              /* A */
	SSL_EN_RC4_128_EXPORT40_WITH_MD5,     /* B */
	SSL_EN_RC2_128_CBC_WITH_MD5,          /* C */
	SSL_EN_RC2_128_CBC_EXPORT40_WITH_MD5, /* D */
	SSL_EN_DES_64_CBC_WITH_MD5,           /* E */
	SSL_EN_DES_192_EDE3_CBC_WITH_MD5,     /* F */
	0
};

int ssl3CipherSuites[] = {
	-1, /* SSL_FORTEZZA_DMS_WITH_FORTEZZA_CBC_SHA  a */
	-1, /* SSL_FORTEZZA_DMS_WITH_RC4_128_SHA * b */
	SSL_RSA_WITH_RC4_128_MD5,               /* c */
	SSL_RSA_WITH_3DES_EDE_CBC_SHA,          /* d */
	SSL_RSA_WITH_DES_CBC_SHA,               /* e */
	SSL_RSA_EXPORT_WITH_RC4_40_MD5,         /* f */
	SSL_RSA_EXPORT_WITH_RC2_CBC_40_MD5,     /* g */
	-1, /* SSL_FORTEZZA_DMS_WITH_NULL_SHA,   * h */
	SSL_RSA_WITH_NULL_MD5,                  /* i */
	SSL_RSA_FIPS_WITH_3DES_EDE_CBC_SHA,     /* j */
	SSL_RSA_FIPS_WITH_DES_CBC_SHA,          /* k */
	TLS_RSA_EXPORT1024_WITH_DES_CBC_SHA,    /* l */
	TLS_RSA_EXPORT1024_WITH_RC4_56_SHA,     /* m */
	0
};

/**************************************************************************
** 
** SSL callback routines.
**
**************************************************************************/

/* Function: char * myPasswd()
 * 
 * Purpose: This function is our custom password handler that is called by
 * SSL when retreiving private certs and keys from the database. Returns a
 * pointer to a string that with a password for the database. Password pointer
 * should point to dynamically allocated memory that will be freed later.
 */
char *
myPasswd(PK11SlotInfo *info, PRBool retry, void *arg)
{
	char * passwd = NULL;

	if ( (!retry) && arg ) {
		passwd = PORT_Strdup((char *)arg);
	}

	return passwd;
}

/* Function: SECStatus myAuthCertificate()
 *
 * Purpose: This function is our custom certificate authentication handler.
 * 
 * Note: This implementation is essentially the same as the default 
 *       SSL_AuthCertificate().
 */
SECStatus 
myAuthCertificate(void *arg, PRFileDesc *socket, 
                  PRBool checksig, PRBool isServer) 
{

	SECCertUsage        certUsage;
	CERTCertificate *   cert;
	void *              pinArg;
	char *              hostName;
	SECStatus           secStatus;

	if (!arg || !socket) {
		errWarn("myAuthCertificate");
		return SECFailure;
	}

	/* Define how the cert is being used based upon the isServer flag. */

	certUsage = isServer ? certUsageSSLClient : certUsageSSLServer;

	cert = SSL_PeerCertificate(socket);
	
	pinArg = SSL_RevealPinArg(socket);

	secStatus = CERT_VerifyCertNow((CERTCertDBHandle *)arg,
	                               cert,
	                               checksig,
	                               certUsage,
	                               pinArg);

	/* If this is a server, we're finished. */
	if (isServer || secStatus != SECSuccess) {
		CERT_DestroyCertificate(cert);
		return secStatus;
	}

	/* Certificate is OK.  Since this is the client side of an SSL
	 * connection, we need to verify that the name field in the cert
	 * matches the desired hostname.  This is our defense against
	 * man-in-the-middle attacks.
	 */

	/* SSL_RevealURL returns a hostName, not an URL. */
	hostName = SSL_RevealURL(socket);

	if (hostName && hostName[0]) {
		secStatus = CERT_VerifyCertName(cert, hostName);
	} else {
		PR_SetError(SSL_ERROR_BAD_CERT_DOMAIN, 0);
		secStatus = SECFailure;
	}

	if (hostName)
		PR_Free(hostName);

	CERT_DestroyCertificate(cert);
	return secStatus;
}

/* Function: SECStatus myBadCertHandler()
 *
 * Purpose: This callback is called when the incoming certificate is not
 * valid. We define a certain set of parameters that still cause the
 * certificate to be "valid" for this session, and return SECSuccess to cause
 * the server to continue processing the request when any of these conditions
 * are met. Otherwise, SECFailure is return and the server rejects the 
 * request.
 */
SECStatus 
myBadCertHandler(void *arg, PRFileDesc *socket) 
{

    SECStatus	secStatus = SECFailure;
    PRErrorCode	err;

    /* log invalid cert here */

    if (!arg) {
		return secStatus;
    }

    *(PRErrorCode *)arg = err = PORT_GetError();

    /* If any of the cases in the switch are met, then we will proceed   */
    /* with the processing of the request anyway. Otherwise, the default */	
    /* case will be reached and we will reject the request.              */

    switch (err) {
    case SEC_ERROR_INVALID_AVA:
    case SEC_ERROR_INVALID_TIME:
    case SEC_ERROR_BAD_SIGNATURE:
    case SEC_ERROR_EXPIRED_CERTIFICATE:
    case SEC_ERROR_UNKNOWN_ISSUER:
    case SEC_ERROR_UNTRUSTED_CERT:
    case SEC_ERROR_CERT_VALID:
    case SEC_ERROR_EXPIRED_ISSUER_CERTIFICATE:
    case SEC_ERROR_CRL_EXPIRED:
    case SEC_ERROR_CRL_BAD_SIGNATURE:
    case SEC_ERROR_EXTENSION_VALUE_INVALID:
    case SEC_ERROR_CA_CERT_INVALID:
    case SEC_ERROR_CERT_USAGES_INVALID:
    case SEC_ERROR_UNKNOWN_CRITICAL_EXTENSION:
		secStatus = SECSuccess;
	break;
    default:
		secStatus = SECFailure;
	break;
    }

	printf("Bad certificate: %d, %s\n", err, SSL_Strerror(err));

    return secStatus;
}

/* Function: SECStatus ownGetClientAuthData()
 *
 * Purpose: This callback is used by SSL to pull client certificate 
 * information upon server request.
 */
SECStatus 
myGetClientAuthData(void *arg,
                    PRFileDesc *socket,
                    struct CERTDistNamesStr *caNames,
                    struct CERTCertificateStr **pRetCert,
                    struct SECKEYPrivateKeyStr **pRetKey) 
{

    CERTCertificate *  cert;
    SECKEYPrivateKey * privKey;
    char *             chosenNickName = (char *)arg;
    void *             proto_win      = NULL;
    SECStatus          secStatus      = SECFailure;

    proto_win = SSL_RevealPinArg(socket);

    if (chosenNickName) {
		cert = PK11_FindCertFromNickname(chosenNickName, proto_win);
		if (cert) {
		    privKey = PK11_FindKeyByAnyCert(cert, proto_win);
		    if (privKey) {
				secStatus = SECSuccess;
		    } else {
				CERT_DestroyCertificate(cert);
		    }
		}
    } else { /* no nickname given, automatically find the right cert */
	CERTCertNicknames *names;
	int                i;

	names = CERT_GetCertNicknames(CERT_GetDefaultCertDB(), 
				      SEC_CERT_NICKNAMES_USER, proto_win);

	if (names != NULL) {
	    for(i = 0; i < names->numnicknames; i++ ) {

		cert = PK11_FindCertFromNickname(names->nicknames[i], 
						 proto_win);
		if (!cert) {
		    continue;
		}

		/* Only check unexpired certs */
		if (CERT_CheckCertValidTimes(cert, PR_Now(), PR_FALSE)
		      != secCertTimeValid ) {
		    CERT_DestroyCertificate(cert);
		    continue;
		}

		secStatus = NSS_CmpCertChainWCANames(cert, caNames);
		if (secStatus == SECSuccess) {
		    privKey = PK11_FindKeyByAnyCert(cert, proto_win);
		    if (privKey) {
			break;
		    }
		    secStatus = SECFailure;
		    break;
		}
	    } /* for loop */
	    CERT_FreeNicknames(names);
	}
    }

    if (secStatus == SECSuccess) {
		*pRetCert = cert;
		*pRetKey  = privKey;
    }

    return secStatus;
}

/* Function: SECStatus myHandshakeCallback()
 *
 * Purpose: Called by SSL to inform application that the handshake is
 * complete. This function is mostly used on the server side of an SSL
 * connection, although it is provided for a client as well.
 * Useful when a non-blocking SSL_ReHandshake or SSL_ResetHandshake 
 * is used to initiate a handshake.
 *
 * A typical scenario would be:
 *
 * 1. Server accepts an SSL connection from the client without client auth.
 * 2. Client sends a request.
 * 3. Server determines that to service request it needs to authenticate the
 * client and initiates another handshake requesting client auth.
 * 4. While handshake is in progress, server can do other work or spin waiting
 * for the handshake to complete.
 * 5. Server is notified that handshake has been successfully completed by
 * the custom handshake callback function and it can service the client's
 * request.
 *
 * Note: This function is not implemented in this sample, as we are using
 * blocking sockets.
 */
void
myHandshakeCallback(PRFileDesc *socket, void *arg) 
{
    printf("Handshake has completed, ready to send data securely.\n");
}


/**************************************************************************
** 
** Routines for disabling SSL ciphers.
**
**************************************************************************/

void
disableAllSSLCiphers(void)
{
    const PRUint16 *cipherSuites = SSL_ImplementedCiphers;
    int             i            = SSL_NumImplementedCiphers;
    SECStatus       rv;

    /* disable all the SSL3 cipher suites */
    while (--i >= 0) {
	PRUint16 suite = cipherSuites[i];
        rv = SSL_CipherPrefSetDefault(suite, PR_FALSE);
	if (rv != SECSuccess) {
	    printf("SSL_CipherPrefSetDefault didn't like value 0x%04x (i = %d)\n",
	    	   suite, i);
	    errWarn("SSL_CipherPrefSetDefault");
	    exit(2);
	}
    }
}

/**************************************************************************
** 
** Error and information routines.
**
**************************************************************************/

void
errWarn(char *function)
{
	PRErrorCode  errorNumber = PR_GetError();
	const char * errorString = SSL_Strerror(errorNumber);

	printf("Error in function %s: %d\n - %s\n",
			function, errorNumber, errorString);
}

void
exitErr(char *function)
{
	errWarn(function);
	/* Exit gracefully. */
	/* ignoring return value of NSS_Shutdown as code exits with 1*/
	(void) NSS_Shutdown();
	PR_Cleanup();
	exit(1);
}

void 
printSecurityInfo(PRFileDesc *fd)
{
	char * cp;	/* bulk cipher name */
	char * ip;	/* cert issuer DN */
	char * sp;	/* cert subject DN */
	int    op;	/* High, Low, Off */
	int    kp0;	/* total key bits */
	int    kp1;	/* secret key bits */
	int    result;
	SSL3Statistics * ssl3stats = SSL_GetStatistics();

	result = SSL_SecurityStatus(fd, &op, &cp, &kp0, &kp1, &ip, &sp);
	if (result != SECSuccess)
		return;
	printf("bulk cipher %s, %d secret key bits, %d key bits, status: %d\n"
		   "subject DN: %s\n"
	   "issuer	DN: %s\n", cp, kp1, kp0, op, sp, ip);
	PR_Free(cp);
	PR_Free(ip);
	PR_Free(sp);

	printf("%ld cache hits; %ld cache misses, %ld cache not reusable\n",
		ssl3stats->hch_sid_cache_hits, ssl3stats->hch_sid_cache_misses,
	ssl3stats->hch_sid_cache_not_ok);

}


/**************************************************************************
** Begin thread management routines and data.
**************************************************************************/

void
thread_wrapper(void * arg)
{
	GlobalThreadMgr *threadMGR = (GlobalThreadMgr *)arg;
	perThread *slot = &threadMGR->threads[threadMGR->index];

	/* wait for parent to finish launching us before proceeding. */
	PR_Lock(threadMGR->threadLock);
	PR_Unlock(threadMGR->threadLock);

	slot->rv = (* slot->startFunc)(slot->a, slot->b);

	PR_Lock(threadMGR->threadLock);
	slot->running = rs_zombie;

	/* notify the thread exit handler. */
	PR_NotifyCondVar(threadMGR->threadEndQ);

	PR_Unlock(threadMGR->threadLock);
}

SECStatus
launch_thread(GlobalThreadMgr *threadMGR,
              startFn         *startFunc,
              void            *a,
              int              b)
{
	perThread *slot;
	int        i;

	if (!threadMGR->threadStartQ) {
		threadMGR->threadLock   = PR_NewLock();
		threadMGR->threadStartQ = PR_NewCondVar(threadMGR->threadLock);
		threadMGR->threadEndQ   = PR_NewCondVar(threadMGR->threadLock);
	}
	PR_Lock(threadMGR->threadLock);
	while (threadMGR->numRunning >= MAX_THREADS) {
		PR_WaitCondVar(threadMGR->threadStartQ, PR_INTERVAL_NO_TIMEOUT);
	}
	for (i = 0; i < threadMGR->numUsed; ++i) {
		slot = &threadMGR->threads[i];
		if (slot->running == rs_idle) 
			break;
	}
	if (i >= threadMGR->numUsed) {
		if (i >= MAX_THREADS) {
			/* something's really wrong here. */
			PORT_Assert(i < MAX_THREADS);
			PR_Unlock(threadMGR->threadLock);
			return SECFailure;
		}
		++(threadMGR->numUsed);
		PORT_Assert(threadMGR->numUsed == i + 1);
		slot = &threadMGR->threads[i];
	}

	slot->a = a;
	slot->b = b;
	slot->startFunc = startFunc;

	threadMGR->index = i;

	slot->prThread = PR_CreateThread(PR_USER_THREAD,
	                                 thread_wrapper, threadMGR,
	                                 PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
	                                 PR_JOINABLE_THREAD, 0);

	if (slot->prThread == NULL) {
		PR_Unlock(threadMGR->threadLock);
		printf("Failed to launch thread!\n");
		return SECFailure;
	} 

	slot->inUse   = 1;
	slot->running = 1;
	++(threadMGR->numRunning);
	PR_Unlock(threadMGR->threadLock);
	printf("Launched thread in slot %d \n", threadMGR->index);

	return SECSuccess;
}

SECStatus 
reap_threads(GlobalThreadMgr *threadMGR)
{
	perThread * slot;
	int			i;

	if (!threadMGR->threadLock)
		return 0;
	PR_Lock(threadMGR->threadLock);
	while (threadMGR->numRunning > 0) {
	    PR_WaitCondVar(threadMGR->threadEndQ, PR_INTERVAL_NO_TIMEOUT);
	    for (i = 0; i < threadMGR->numUsed; ++i) {
		slot = &threadMGR->threads[i];
		if (slot->running == rs_zombie)  {
		    /* Handle cleanup of thread here. */
		    printf("Thread in slot %d returned %d\n", i, slot->rv);

		    /* Now make sure the thread has ended OK. */
		    PR_JoinThread(slot->prThread);
		    slot->running = rs_idle;
		    --threadMGR->numRunning;

		    /* notify the thread launcher. */
		    PR_NotifyCondVar(threadMGR->threadStartQ);
		}
	    }
	}

	/* Safety Sam sez: make sure count is right. */
	for (i = 0; i < threadMGR->numUsed; ++i) {
		slot = &threadMGR->threads[i];
		if (slot->running != rs_idle)  {
			fprintf(stderr, "Thread in slot %d is in state %d!\n", 
			                 i, slot->running);
		}
	}
	PR_Unlock(threadMGR->threadLock);
	return 0;
}

void
destroy_thread_data(GlobalThreadMgr *threadMGR)
{
	PORT_Memset(threadMGR->threads, 0, sizeof(threadMGR->threads));

	if (threadMGR->threadEndQ) {
		PR_DestroyCondVar(threadMGR->threadEndQ);
		threadMGR->threadEndQ = NULL;
	}
	if (threadMGR->threadStartQ) {
		PR_DestroyCondVar(threadMGR->threadStartQ);
		threadMGR->threadStartQ = NULL;
	}
	if (threadMGR->threadLock) {
		PR_DestroyLock(threadMGR->threadLock);
		threadMGR->threadLock = NULL;
	}
}

/**************************************************************************
** End	 thread management routines.
**************************************************************************/

void 
lockedVars_Init( lockedVars * lv)
{
	lv->count	= 0;
	lv->waiters = 0;
	lv->lock	= PR_NewLock();
	lv->condVar = PR_NewCondVar(lv->lock);
}

void
lockedVars_Destroy( lockedVars * lv)
{
	PR_DestroyCondVar(lv->condVar);
	lv->condVar = NULL;

	PR_DestroyLock(lv->lock);
	lv->lock = NULL;
}

void
lockedVars_WaitForDone(lockedVars * lv)
{
	PR_Lock(lv->lock);
	while (lv->count > 0) {
		PR_WaitCondVar(lv->condVar, PR_INTERVAL_NO_TIMEOUT);
	}
	PR_Unlock(lv->lock);
}

int	/* returns count */
lockedVars_AddToCount(lockedVars * lv, int addend)
{
	int rv;

	PR_Lock(lv->lock);
	rv = lv->count += addend;
	if (rv <= 0) {
	PR_NotifyCondVar(lv->condVar);
	}
	PR_Unlock(lv->lock);
	return rv;
}
