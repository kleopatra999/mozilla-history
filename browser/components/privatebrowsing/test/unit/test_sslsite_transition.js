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
 * The Original Code is Private Browsing Tests.
 *
 * The Initial Developer of the Original Code is
 * Ehsan Akhgari.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Ehsan Akhgari <ehsan.akhgari@gmail.com> (Original Author)
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

// This tests the private browsing service to make sure it switches the offline
// status as expected (see bug 463256).

function run_test_on_service() {
  // initialization
  var pb = Cc[PRIVATEBROWSING_CONTRACT_ID].
           getService(Ci.nsIPrivateBrowsingService);
  var os = Cc["@mozilla.org/observer-service;1"].
           getService(Ci.nsIObserverService);
  var prefBranch = Cc["@mozilla.org/preferences-service;1"].
                   getService(Ci.nsIPrefBranch);
  prefBranch.setBoolPref("browser.privatebrowsing.keep_current_session", true);

  var observer = {
    observe: function (aSubject, aTopic, aData) {
      if (aTopic == "network:offline-status-changed")
        this.events.push(aData);
    },
    events: []
  };
  os.addObserver(observer, "network:offline-status-changed", false);

  // enter the private browsing mode, and wait for the about:pb page to load
  pb.privateBrowsingEnabled = true;
  do_check_eq(observer.events.length, 2);
  do_check_eq(observer.events[0], "offline");
  do_check_eq(observer.events[1], "online");

  // leave the private browsing mode, and wait for the SSL page to load again
  pb.privateBrowsingEnabled = false;
  do_check_eq(observer.events.length, 4);
  do_check_eq(observer.events[2], "offline");
  do_check_eq(observer.events[3], "online");

  os.removeObserver(observer, "network:offline-status-changed", false);
  prefBranch.clearUserPref("browser.privatebrowsing.keep_current_session");
}

// Support running tests on both the service itself and its wrapper
function run_test() {
  run_test_on_all_services();
}