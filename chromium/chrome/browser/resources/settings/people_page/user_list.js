// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview
 * 'settings-user-list' shows a list of users whitelisted on this Chrome OS
 * device.
 *
 * Example:
 *
 *    <settings-user-list prefs="{{prefs}}">
 *    </settings-user-list>
 */
Polymer({
  is: 'settings-user-list',

  behaviors: [I18nBehavior, settings.RouteObserverBehavior],

  properties: {
    /**
     * Current list of whitelisted users.
     * @private {!Array<!chrome.usersPrivate.User>}
     */
    users_: {
      type: Array,
      value: function() {
        return [];
      },
      notify: true
    },

    /**
     * Whether the user list is disabled, i.e. that no modifications can be
     * made.
     * @type {boolean}
     */
    disabled: {
      type: Boolean,
      value: false,
    }
  },

  /** @override */
  ready: function() {
    chrome.settingsPrivate.onPrefsChanged.addListener(function(prefs) {
      prefs.forEach(function(pref) {
        if (pref.key == 'cros.accounts.users') {
          chrome.usersPrivate.getWhitelistedUsers(function(users) {
            this.setUsers_(users);
          }.bind(this));
        }
      }, this);
    }.bind(this));
  },

  /** @protected */
  currentRouteChanged: function() {
    if (settings.getCurrentRoute() == settings.Route.ACCOUNTS) {
      chrome.usersPrivate.getWhitelistedUsers(function(users) {
        this.setUsers_(users);
      }.bind(this));
    }
  },

  /**
   * @param {!chrome.usersPrivate.User} user
   * @return {string}
   * @private
   */
  getUserName_: function(user) {
    return user.isOwner ? this.i18n('deviceOwnerLabel', user.name) : user.name;
  },

  /**
   * Helper function that sorts and sets the given list of whitelisted users.
   * @param {!Array<!chrome.usersPrivate.User>} users List of whitelisted users.
   */
  setUsers_: function(users) {
    this.users_ = users;
    this.users_.sort(function(a, b) {
      if (a.isOwner != b.isOwner)
        return b.isOwner ? 1 : -1;
      else
        return -1;
    });
  },

  /**
   * @private
   * @param {!{model: !{item: !chrome.usersPrivate.User}}} e
   */
  removeUser_: function(e) {
    chrome.usersPrivate.removeWhitelistedUser(
        e.model.item.email, /* callback */ function() {});
  },

  /** @private */
  shouldHideCloseButton_: function(disabled, isUserOwner) {
    return disabled || isUserOwner;
  },

  /**
   * @param {chrome.usersPrivate.User} user
   * @private
   */
  getProfilePictureUrl_: function(user) {
    return 'chrome://userimage/' + user.email + '?id=' + Date.now();
  }
});
