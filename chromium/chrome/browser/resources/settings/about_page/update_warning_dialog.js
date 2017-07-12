// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview 'settings-update-warning-dialog' is a component warning the
 * user about update over mobile data. By clicking 'Continue', the user
 * agrees to download update using mobile data.
 */
Polymer({
  is: 'settings-update-warning-dialog',

  behaviors: [I18nBehavior],

  /** @override */
  ready: function() {
    this.browserProxy_ = settings.AboutPageBrowserProxyImpl.getInstance();
  },

  /** @override */
  attached: function() {
    this.$.dialog.showModal();
  },

  /** @private */
  onCancelTap_: function() {
    // TODO(weidongg): implement the real behaviors here.
    this.$.dialog.close();
  },

  /** @private */
  onContinueTap_: function() {
    // TODO(weidongg): implement the real behaviors here.
    this.$.dialog.close();
  },

  /**
   * @param {string} updateSizeMb Size of the update in megabytes.
   * @private
   */
  setUpdateWarningMessage: function(updateSizeMb) {
     this.$$("#update-warning-message").innerHTML =
         this.i18n("aboutUpdateWarningMessage", updateSizeMb);
  },
});
