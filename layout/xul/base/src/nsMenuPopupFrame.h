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
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Original Author: David W. Hyatt (hyatt@netscape.com)
 *   Mike Pinkerton (pinkerton@netscape.com)
 *   Dean Tessman <dean_tessman@hotmail.com>
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

//
// nsMenuPopupFrame
//

#ifndef nsMenuPopupFrame_h__
#define nsMenuPopupFrame_h__

#include "prtypes.h"
#include "nsIAtom.h"
#include "nsGkAtoms.h"
#include "nsCOMPtr.h"
#include "nsMenuFrame.h"
#include "nsIDOMEventTarget.h"

#include "nsBoxFrame.h"
#include "nsIMenuParent.h"
#include "nsIWidget.h"

#include "nsITimer.h"

enum nsPopupType {
  ePopupTypePanel,
  ePopupTypeMenu,
  ePopupTypeTooltip
};

// values are selected so that the direction can be flipped just by
// changing the sign
#define POPUPALIGNMENT_NONE 0
#define POPUPALIGNMENT_TOPLEFT 1
#define POPUPALIGNMENT_TOPRIGHT -1
#define POPUPALIGNMENT_BOTTOMLEFT 2
#define POPUPALIGNMENT_BOTTOMRIGHT -2

#define INC_TYP_INTERVAL  1000  // 1s. If the interval between two keypresses is shorter than this, 
                                //   treat as a continue typing
// XXX, kyle.yuan@sun.com, there are 4 definitions for the same purpose:
//  nsMenuPopupFrame.h, nsListControlFrame.cpp, listbox.xml, tree.xml
//  need to find a good place to put them together.
//  if someone changes one, please also change the other.

nsIFrame* NS_NewMenuPopupFrame(nsIPresShell* aPresShell, nsStyleContext* aContext);

class nsIViewManager;
class nsIView;
class nsIMenuParent;
class nsMenuPopupFrame;

class nsMenuPopupFrame : public nsBoxFrame, public nsIMenuParent
{
public:
  nsMenuPopupFrame(nsIPresShell* aShell, nsStyleContext* aContext);

  // nsIMenuParentInterface
  virtual nsMenuFrame* GetCurrentMenuItem();
  NS_IMETHOD SetCurrentMenuItem(nsMenuFrame* aMenuItem);
  virtual void CurrentMenuIsBeingDestroyed();
  NS_IMETHOD ChangeMenuItem(nsMenuFrame* aMenuItem, PRBool aSelectFirstItem);

  // as popups are opened asynchronously, the popup pending state is used to
  // prevent multiple requests from attempting to open the same popup twice
  PRBool IsOpenPending() { return mIsOpenPending; }
  void ClearOpenPending() { mIsOpenPending = PR_FALSE; }

  NS_IMETHOD SetActive(PRBool aActiveFlag) { return NS_OK; } // We don't care.
  virtual PRBool IsActive() { return PR_FALSE; }
  virtual PRBool IsMenuBar() { return PR_FALSE; }

  /*
   * When this popup is open, should clicks outside of it be consumed?
   * Return PR_TRUE if the popup should rollup on an outside click, 
   * but consume that click so it can't be used for anything else.
   * Return PR_FALSE to allow clicks outside the popup to activate content 
   * even when the popup is open.
   * ---------------------------------------------------------------------
   * 
   * Should clicks outside of a popup be eaten?
   *
   *       Menus     Autocomplete     Comboboxes
   * Mac     Eat           No              Eat
   * Win     No            No              Eat     
   * Unix    Eat           No              Eat
   *
   */
  PRBool ConsumeOutsideClicks();

  virtual PRBool IsContextMenu() { return mIsContextMenu; }

  virtual PRBool MenuClosed() { return PR_TRUE; }

  NS_IMETHOD GetWidget(nsIWidget **aWidget);

  // The dismissal listener gets created and attached to the window.
  void AttachedDismissalListener();

  // Overridden methods
  NS_IMETHOD Init(nsIContent*      aContent,
                  nsIFrame*        aParent,
                  nsIFrame*        aPrevInFlow);

  NS_IMETHOD AttributeChanged(PRInt32 aNameSpaceID,
                              nsIAtom* aAttribute,
                              PRInt32 aModType);

  virtual void Destroy();

  virtual void InvalidateInternal(const nsRect& aDamageRect,
                                  nscoord aX, nscoord aY, nsIFrame* aForChild,
                                  PRBool aImmediate);

  void EnsureWidget();

  virtual nsresult CreateWidgetForView(nsIView* aView);

  NS_IMETHOD SetInitialChildList(nsIAtom*        aListName,
                                 nsIFrame*       aChildList);

  virtual PRBool IsLeaf() const
  {
    if (!mGeneratedChildren && mPopupType == ePopupTypeMenu) {
      // menu popups generate their child frames lazily only when opened, so
      // behave like a leaf frame. However, generate child frames normally if
      // the parent menu has a sizetopopup attribute. In this case the size of
      // the parent menu is dependant on the size of the popup, so the frames
      // need to exist in order to calculate this size.
      nsIContent* parentContent = mContent->GetParent();
      if (parentContent &&
          !parentContent->HasAttr(kNameSpaceID_None, nsGkAtoms::sizetopopup))
        return PR_TRUE;
    }

    return PR_FALSE;
  }

  // AdjustView should be called by the parent frame after the popup has been
  // laid out, so that the view can be shown.
  void AdjustView();

  void GetViewOffset(nsIView* aView, nsPoint& aPoint);
  nsIView* GetRootViewForPopup(nsIFrame* aStartFrame,
                               PRBool aStopAtViewManagerRoot);

  // set the position of the popup either relative to the anchor aAnchorFrame
  // (or the frame for mAnchorContent if aAnchorFrame is null) or at a specific
  // point if a screen position (mScreenXPos and mScreenYPos) are set. The popup
  // will be adjusted so that it is on screen.
  nsresult SetPopupPosition(nsIFrame* aAnchorFrame);

  PRBool HasGeneratedChildren() { return mGeneratedChildren; }
  void SetGeneratedChildren() { mGeneratedChildren = PR_TRUE; }

  // called when the Enter key is pressed while the popup is open. This will
  // just pass the call down to the current menu, if any. Also, calling Enter
  // will reset the current incremental search string, calculated in
  // FindMenuWithShortcut
  nsMenuFrame* Enter();

  PRInt32 PopupType() const { return mPopupType; }
  PRBool IsMenu() { return mPopupType == ePopupTypeMenu; }
  PRBool IsOpen() { return mIsOpen; }
  PRBool HasOpenChanged() { return mIsOpenChanged; }

  // the Initialize methods are used to set the anchor position for
  // each way of opening a popup.
  void InitializePopup(nsIContent* aAnchorContent,
                       const nsAString& aPosition,
                       PRInt32 aXPos, PRInt32 aYPos,
                       PRBool aAttributesOverride);

  void InitializePopupAtScreen(PRInt32 aXPos, PRInt32 aYPos);

  void InitializePopupWithAnchorAlign(nsIContent* aAnchorContent,
                                      nsAString& aAnchor,
                                      nsAString& aAlign,
                                      PRInt32 aXPos, PRInt32 aYPos);

  // indicate that the popup should be opened
  PRBool ShowPopup(PRBool aIsContextMenu, PRBool aSelectFirstItem);
  // indicate that the popup should be hidden
  void HidePopup(PRBool aDeselectMenu);

  // locate and return the menu frame that should be activated for the
  // supplied key event. If doAction is set to true by this method,
  // then the menu's action should be carried out, as if the user had pressed
  // the Enter key. If doAction is false, the menu should just be highlighted.
  // This method also handles incremental searching in menus so the user can
  // type the first few letters of an item/s name to select it.
  nsMenuFrame* FindMenuWithShortcut(nsIDOMKeyEvent* aKeyEvent, PRBool& doAction);

  void ClearIncrementalString() { mIncrementalString.Truncate(); }

  virtual nsIAtom* GetType() const { return nsGkAtoms::menuPopupFrame; }

#ifdef DEBUG
  NS_IMETHOD GetFrameName(nsAString& aResult) const
  {
      return MakeFrameName(NS_LITERAL_STRING("MenuPopup"), aResult);
  }
#endif

  void EnsureMenuItemIsVisible(nsMenuFrame* aMenuFrame);

  // This sets 'left' and 'top' attributes.
  // May kill the frame.
  void MoveTo(PRInt32 aLeft, PRInt32 aTop);

  void GetAutoPosition(PRBool* aShouldAutoPosition);
  void SetAutoPosition(PRBool aShouldAutoPosition);
  void SetConsumeRollupEvent(PRUint32 aConsumeMode);

  nsIScrollableView* GetScrollableView(nsIFrame* aStart);
  
protected:
  // Move without updating attributes.                                          
  void MoveToInternal(PRInt32 aLeft, PRInt32 aTop);                             

  // redefine to tell the box system not to move the views.
  virtual void GetLayoutFlags(PRUint32& aFlags);

  void InitPositionFromAnchorAlign(const nsAString& aAnchor,
                                   const nsAString& aAlign);

  void AdjustPositionForAnchorAlign ( PRInt32* ioXPos, PRInt32* ioYPos, const nsRect & inParentRect,
                                      PRBool* outFlushWithTopBottom ) ;

  PRBool IsMoreRoomOnOtherSideOfParent ( PRBool inFlushAboveBelow, PRInt32 inScreenViewLocX, PRInt32 inScreenViewLocY,
                                           const nsRect & inScreenParentFrameRect, PRInt32 inScreenTopTwips, PRInt32 inScreenLeftTwips,
                                           PRInt32 inScreenBottomTwips, PRInt32 inScreenRightTwips ) ;

  void MovePopupToOtherSideOfParent ( PRBool inFlushAboveBelow, PRInt32* ioXPos, PRInt32* ioYPos, 
                                           PRInt32* ioScreenViewLocX, PRInt32* ioScreenViewLocY,
                                           const nsRect & inScreenParentFrameRect, PRInt32 inScreenTopTwips, PRInt32 inScreenLeftTwips,
                                           PRInt32 inScreenBottomTwips, PRInt32 inScreenRightTwips ) ;

  // Move the popup to the position specified in its |left| and |top| attributes.
  void MoveToAttributePosition();

  // the content that the popup is anchored to, if any, which may be in a
  // different document than the popup.
  nsCOMPtr<nsIContent> mAnchorContent;

  nsMenuFrame* mCurrentMenu; // The current menu that is active.

  // popup alignment relative to the anchor node
  PRInt8 mPopupAlignment;
  PRInt8 mPopupAnchor;

  // the position of the popup. The screen coordinates, if set to values other
  // than -1, override mXPos and mYPos.
  PRInt32 mXPos;
  PRInt32 mYPos;
  PRInt32 mScreenXPos;
  PRInt32 mScreenYPos;

  nsPopupType mPopupType; // type of popup

  PRPackedBool mIsOpen;  // true if the popup is open
  PRPackedBool mIsOpenChanged; // true if the open state changed since the last layout
  PRPackedBool mIsOpenPending; // true if an open is pending
  PRPackedBool mIsContextMenu; // true for context menus
  PRPackedBool mGeneratedChildren; // true if the contents have been created

  PRPackedBool mMenuCanOverlapOSBar;    // can we appear over the taskbar/menubar?
  PRPackedBool mShouldAutoPosition; // Should SetPopupPosition be allowed to auto position popup?
  PRPackedBool mConsumeRollupEvent; // Should the rollup event be consumed?
  PRPackedBool mInContentShell; // True if the popup is in a content shell

  nsString     mIncrementalString;  // for incremental typing navigation

}; // class nsMenuPopupFrame

#endif
