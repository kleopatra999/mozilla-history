/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

function createEmptyGroupItem(contentWindow, width, height, padding, animate) {
  let pageBounds = contentWindow.Items.getPageBounds();
  pageBounds.inset(padding, padding);

  let box = new contentWindow.Rect(pageBounds);
  box.width = width;
  box.height = height;

  let emptyGroupItem =
    new contentWindow.GroupItem([], { bounds: box, immediately: !animate });

  return emptyGroupItem;
}

// ----------
function createGroupItemWithTabs(win, width, height, padding, urls, animate) {
  let contentWindow = win.TabView.getContentWindow();
  let groupItemCount = contentWindow.GroupItems.groupItems.length;
  // create empty group item
  let groupItem = createEmptyGroupItem(contentWindow, width, height, padding, animate);
  ok(groupItem.isEmpty(), "This group is empty");
  is(contentWindow.GroupItems.groupItems.length, ++groupItemCount,
     "The number of groups is increased by 1");
  // add blank items
  contentWindow.UI.setActive(groupItem);
  let t = 0;
  urls.forEach( function(url) {
    let newItem = win.gBrowser.loadOneTab(url)._tabViewTabItem;
    ok(newItem.container, "Created element "+t+":"+newItem.container);
    ++t;
  });
  // to set one of tabItem to be active since we load tabs into a group 
  // in a non-standard flow.
  contentWindow.UI.setActive(groupItem);
  return groupItem;
}

// ----------
function createGroupItemWithBlankTabs(win, width, height, padding, numNewTabs, animate) {
  let urls = [];
  while(numNewTabs--)
    urls.push("about:blank");
  return createGroupItemWithTabs(win, width, height, padding, urls, animate);
}

// ----------
function closeGroupItem(groupItem, callback) {
  groupItem.addSubscriber("close", function onClose() {
    groupItem.removeSubscriber("close", onClose);
    if ("function" == typeof callback)
      executeSoon(callback);
  });

  if (groupItem.getChildren().length) {
    groupItem.addSubscriber("groupHidden", function onHide() {
      groupItem.removeSubscriber("groupHidden", onHide);
      groupItem.closeHidden();
    });
  }

  groupItem.closeAll();
}

// ----------
function afterAllTabItemsUpdated(callback, win) {
  win = win || window;
  let tabItems = win.document.getElementById("tab-view").contentWindow.TabItems;

  for (let a = 0; a < win.gBrowser.tabs.length; a++) {
    let tabItem = win.gBrowser.tabs[a]._tabViewTabItem;
    if (tabItem)
      tabItems._update(win.gBrowser.tabs[a]);
  }
  callback();
}

// ---------
function newWindowWithTabView(shownCallback, loadCallback, width, height) {
  let winWidth = width || 800;
  let winHeight = height || 800;
  let win = window.openDialog(getBrowserURL(), "_blank",
                              "chrome,all,dialog=no,height=" + winHeight +
                              ",width=" + winWidth);

  whenWindowLoaded(win, function () {
    if (loadCallback)
      loadCallback(win);
  });

  whenDelayedStartupFinished(win, function () {
    showTabView(function () shownCallback(win), win);
  });
}

// ----------
function afterAllTabsLoaded(callback, win) {
  const TAB_STATE_NEEDS_RESTORE = 1;

  win = win || window;

  let stillToLoad = 0;
  let restoreHiddenTabs = Services.prefs.getBoolPref(
                          "browser.sessionstore.restore_hidden_tabs");

  function onLoad() {
    this.removeEventListener("load", onLoad, true);
    stillToLoad--;
    if (!stillToLoad)
      executeSoon(callback);
  }

  for (let a = 0; a < win.gBrowser.tabs.length; a++) {
    let tab = win.gBrowser.tabs[a];
    let browser = tab.linkedBrowser;

    let isRestorable = !(tab.hidden && !restoreHiddenTabs &&
                         browser.__SS_restoreState &&
                         browser.__SS_restoreState == TAB_STATE_NEEDS_RESTORE);

    if (isRestorable && browser.contentDocument.readyState != "complete" ||
        browser.webProgress.isLoadingDocument) {
      stillToLoad++;
      browser.addEventListener("load", onLoad, true);
    }
  }

  if (!stillToLoad)
    executeSoon(callback);
}

// ----------
function showTabView(callback, win) {
  win = win || window;

  if (win.TabView.isVisible()) {
    waitForFocus(callback, win);
    return;
  }

  whenTabViewIsShown(function() {
    waitForFocus(callback, win);
  }, win);
  win.TabView.show();
}

// ----------
function hideTabView(callback, win) {
  win = win || window;

  if (!win.TabView.isVisible()) {
    callback();
    return;
  }

  whenTabViewIsHidden(callback, win);
  win.TabView.hide();
}

// ----------
function whenTabViewIsHidden(callback, win) {
  win = win || window;

  if (!win.TabView.isVisible()) {
    callback();
    return;
  }

  win.addEventListener('tabviewhidden', function () {
    win.removeEventListener('tabviewhidden', arguments.callee, false);
    callback();
  }, false);
}

// ----------
function whenTabViewIsShown(callback, win) {
  win = win || window;

  if (win.TabView.isVisible()) {
    callback();
    return;
  }

  win.addEventListener('tabviewshown', function () {
    win.removeEventListener('tabviewshown', arguments.callee, false);
    callback();
  }, false);
}

// ----------
function showSearch(callback, win) {
  win = win || window;

  let contentWindow = win.TabView.getContentWindow();
  if (contentWindow.isSearchEnabled()) {
    callback();
    return;
  }

  whenSearchIsEnabled(callback, win);
  contentWindow.performSearch();
}

// ----------
function hideSearch(callback, win) {
  win = win || window;

  let contentWindow = win.TabView.getContentWindow();
  if (!contentWindow.isSearchEnabled()) {
    callback();
    return;
  }

  whenSearchIsDisabled(callback, win);
  contentWindow.hideSearch();
}

// ----------
function whenSearchIsEnabled(callback, win) {
  win = win || window;

  let contentWindow = win.TabView.getContentWindow();
  if (contentWindow.isSearchEnabled()) {
    callback();
    return;
  }

  contentWindow.addEventListener("tabviewsearchenabled", function onSearchEnabled() {
    contentWindow.removeEventListener("tabviewsearchenabled", onSearchEnabled, false);
    callback();
  }, false);
}

// ----------
function whenSearchIsDisabled(callback, win) {
  win = win || window;

  let contentWindow = win.TabView.getContentWindow();
  if (!contentWindow.isSearchEnabled()) {
    callback();
    return;
  }

  contentWindow.addEventListener("tabviewsearchdisabled", function onSearchDisabled() {
    contentWindow.removeEventListener("tabviewsearchdisabled", onSearchDisabled, false);
    callback();
  }, false);
}


// ----------
function hideGroupItem(groupItem, callback) {
  if (groupItem.hidden) {
    callback();
    return;
  }

  groupItem.addSubscriber("groupHidden", function onHide() {
    groupItem.removeSubscriber("groupHidden", onHide);
    callback();
  });
  groupItem.closeAll();
}

// ----------
function unhideGroupItem(groupItem, callback) {
  if (!groupItem.hidden) {
    callback();
    return;
  }

  groupItem.addSubscriber("groupShown", function onShown() {
    groupItem.removeSubscriber("groupShown", onShown);
    callback();
  });
  groupItem._unhide();
}

// ----------
function whenWindowLoaded(win, callback) {
  win.addEventListener("load", function onLoad() {
    win.removeEventListener("load", onLoad, false);
    executeSoon(callback);
  }, false);
}

// ----------
function whenWindowStateReady(win, callback) {
  win.addEventListener("SSWindowStateReady", function onReady() {
    win.removeEventListener("SSWindowStateReady", onReady, false);
    executeSoon(callback);
  }, false);
}

// ----------
function whenDelayedStartupFinished(win, callback) {
  let topic = "browser-delayed-startup-finished";
  Services.obs.addObserver(function onStartup(aSubject) {
    if (win != aSubject)
      return;

    Services.obs.removeObserver(onStartup, topic, false);
    executeSoon(callback);
  }, topic, false);
}

// ----------
function newWindowWithState(state, callback) {
  const ss = Cc["@mozilla.org/browser/sessionstore;1"]
             .getService(Ci.nsISessionStore);

  let opts = "chrome,all,dialog=no,height=800,width=800";
  let win = window.openDialog(getBrowserURL(), "_blank", opts);

  let numConditions = 2;
  let check = function () {
    if (!--numConditions)
      callback(win);
  };

  whenWindowLoaded(win, function () {
    whenWindowStateReady(win, function () {
      afterAllTabsLoaded(check, win);
    });

    ss.setWindowState(win, JSON.stringify(state), true);
  });

  whenDelayedStartupFinished(win, check);
}

// ----------
function restoreTab(callback, index, win) {
  win = win || window;

  let tab = win.undoCloseTab(index || 0);
  let tabItem = tab._tabViewTabItem;

  let finalize = function () {
    afterAllTabsLoaded(function () callback(tab), win);
  };

  if (tabItem._reconnected) {
    finalize();
    return;
  }

  tab._tabViewTabItem.addSubscriber("reconnected", function onReconnected() {
    tab._tabViewTabItem.removeSubscriber("reconnected", onReconnected);
    finalize();
  });
}
