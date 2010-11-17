/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
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
 * the Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Daniel Holbert <dholbert@mozilla.com>
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

#ifndef mozilla_imagelib_VectorImage_h_
#define mozilla_imagelib_VectorImage_h_

#include "Image.h"
#include "nsIStreamListener.h"
#include "nsWeakReference.h"

class imgIDecoderObserver;

namespace mozilla {
namespace imagelib {

class SVGDocumentWrapper;
class SVGRootRenderingObserver;

class VectorImage : public Image,
                    public nsIStreamListener
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIREQUESTOBSERVER
  NS_DECL_NSISTREAMLISTENER

  // BEGIN NS_DECL_IMGICONTAINER
  // ** Don't edit this chunk except to mirror changes in imgIContainer.idl **
  NS_SCRIPTABLE NS_IMETHOD GetWidth(PRInt32 *aWidth);
  NS_SCRIPTABLE NS_IMETHOD GetHeight(PRInt32 *aHeight);
  NS_SCRIPTABLE NS_IMETHOD GetType(PRUint16 *aType);
  NS_SCRIPTABLE NS_IMETHOD GetAnimated(PRBool *aAnimated);
  NS_SCRIPTABLE NS_IMETHOD GetCurrentFrameIsOpaque(PRBool *aCurrentFrameIsOpaque);
  NS_IMETHOD GetFrame(PRUint32 aWhichFrame, PRUint32 aFlags, gfxASurface **_retval NS_OUTPARAM);
  NS_IMETHOD CopyFrame(PRUint32 aWhichFrame, PRUint32 aFlags, gfxImageSurface **_retval NS_OUTPARAM);
  NS_IMETHOD ExtractFrame(PRUint32 aWhichFrame, const nsIntRect & aRect, PRUint32 aFlags, imgIContainer **_retval NS_OUTPARAM);
  NS_IMETHOD Draw(gfxContext *aContext, gfxPattern::GraphicsFilter aFilter, const gfxMatrix & aUserSpaceToImageSpace, const gfxRect & aFill, const nsIntRect & aSubimage, const nsIntSize & aViewportSize, PRUint32 aFlags);
  NS_IMETHOD_(nsIFrame *) GetRootLayoutFrame(void);
  NS_SCRIPTABLE NS_IMETHOD RequestDecode(void);
  NS_SCRIPTABLE NS_IMETHOD LockImage(void);
  NS_SCRIPTABLE NS_IMETHOD UnlockImage(void);
  NS_SCRIPTABLE NS_IMETHOD GetAnimationMode(PRUint16 *aAnimationMode);
  NS_SCRIPTABLE NS_IMETHOD SetAnimationMode(PRUint16 aAnimationMode);
  NS_SCRIPTABLE NS_IMETHOD ResetAnimation(void);
  // END NS_DECL_IMGICONTAINER

  VectorImage(imgStatusTracker* aStatusTracker = nsnull);
  virtual ~VectorImage();

  // C++-only version of imgIContainer::GetType, for convenience
  PRUint16 GetType() { return imgIContainer::TYPE_VECTOR; }

  // Methods inherited from Image
  nsresult Init(imgIDecoderObserver* aObserver,
                const char* aMimeType,
                const char* aURIString,
                PRUint32 aFlags);
  void GetCurrentFrameRect(nsIntRect& aRect);
  PRUint32 GetDecodedDataSize();
  PRUint32 GetSourceDataSize();

  // Callback for SVGRootRenderingObserver
  void InvalidateObserver();

protected:
  virtual nsresult StartAnimation();
  virtual nsresult StopAnimation();

private:
  nsWeakPtr                          mObserver;   //! imgIDecoderObserver
  nsRefPtr<SVGDocumentWrapper>       mSVGDocumentWrapper;
#ifdef MOZ_ENABLE_LIBXUL
  nsRefPtr<SVGRootRenderingObserver> mRenderingObserver;
#endif // MOZ_ENABLE_LIBXUL

  nsIntRect      mRestrictedRegion;       // If we were created by
                                          // ExtractFrame, this is the region
                                          // that we're restricted to using.
                                          // Otherwise, this is ignored.

  nsIntSize      mLastRenderedSize;       // The viewport-size that we've
                                          // most recently passed to
                                          // mSVGDocumentWrapper as its
                                          // viewport-bounds.

  PRUint16       mAnimationMode;          // Are we allowed to animate?

  PRPackedBool   mIsInitialized:1;        // Have we been initalized?
  PRPackedBool   mIsFullyLoaded:1;        // Has OnStopRequest been called?
  PRPackedBool   mHaveAnimations:1;       // Is our SVG content SMIL-animated?
                                          // (Only set after mIsFullyLoaded.)
  PRPackedBool   mHaveRestrictedRegion:1; // Are we a restricted-region clone
                                          // created via ExtractFrame?
};

} // namespace imagelib
} // namespace mozilla

#endif // mozilla_imagelib_VectorImage_h_
