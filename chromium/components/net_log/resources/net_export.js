// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * Main entry point called once the page has loaded.
 */
function onLoad() {
  NetExportView.getInstance();
}

document.addEventListener('DOMContentLoaded', onLoad);

/**
 * This class handles the presentation of our profiler view. Used as a
 * singleton.
 */
var NetExportView = (function() {
  'use strict';

  // --------------------------------------------------------------------------

  /**
   * @constructor
   */
  function NetExportView() {
    $('export-view-start-data').onclick = this.onStartData_.bind(this);
    $('export-view-stop-data').onclick = this.onStopData_.bind(this);
    if (this.useMobileUI_())
      $('export-view-mobile-send-data').onclick = this.onSendData_.bind(this);

    // Tell NetExportMessageHandler to notify the UI of future state changes
    // from this point on (through onExportNetLogInfoChanged()).
    chrome.send('enableNotifyUIWithState');
  }

  cr.addSingletonGetter(NetExportView);

  NetExportView.prototype = {
    /**
     * Starts saving NetLog data to a file.
     */
    onStartData_: function() {
      var logMode =
          document.querySelector('input[name="log-mode"]:checked').value;
      chrome.send('startNetLog', [logMode]);
    },

    /**
     * Stops saving NetLog data to a file.
     */
    onStopData_: function() {
      chrome.send('stopNetLog');
    },

    /**
     * Sends NetLog data via email from browser.
     */
    onSendData_: function() {
      chrome.send('sendNetLog');
    },

    /**
     * Updates the UI to reflect the current state. Displays the path name of
     * the file where NetLog data is collected.
     */
    onExportNetLogInfoChanged: function(exportNetLogInfo) {
      if (exportNetLogInfo.file) {
        var message = '';
        if (exportNetLogInfo.state == 'LOGGING')
          message = 'NetLog data is collected in: ';
        else if (exportNetLogInfo.logType != 'NONE')
          message = 'NetLog data to send is in: ';
        $('export-view-file-path-text').textContent =
            message + exportNetLogInfo.file;
      } else {
        $('export-view-file-path-text').textContent = '';
      }

      // Disable all controls.  Useable controls are enabled below.
      var controls = document.querySelectorAll('button, input');
      for (var i = 0; i < controls.length; ++i) {
        controls[i].disabled = true;
      }

      if (this.useMobileUI_()) {
        $('export-view-mobile-deletes-log-text').hidden = true;
        $('export-view-mobile-private-data-text').hidden = true;
        $('export-view-mobile-send-old-log-text').hidden = true;
      }

      if (exportNetLogInfo.state == 'NOT_LOGGING') {
        // Allow making a new log.
        $('export-view-strip-private-data-button').disabled = false;
        $('export-view-include-private-data-button').disabled = false;
        $('export-view-log-bytes-button').disabled = false;
        $('export-view-start-data').disabled = false;

        // If there's a pre-existing log, allow sending it (this only
        // applies to the mobile UI).
        if (this.useMobileUI_() && exportNetLogInfo.logExists) {
          $('export-view-mobile-deletes-log-text').hidden = false;
          $('export-view-mobile-send-data').disabled = false;
          if (!exportNetLogInfo.logCaptureModeKnown) {
            $('export-view-mobile-send-old-log-text').hidden = false;
          } else if (exportNetLogInfo.captureMode != 'STRIP_PRIVATE_DATA') {
            $('export-view-mobile-private-data-text').hidden = false;
          }
        }
      } else if (exportNetLogInfo.state == 'LOGGING') {
        // Only possible to stop logging. Radio buttons reflects current state.
        document
            .querySelector(
                'input[name="log-mode"][value="' +
                exportNetLogInfo.captureMode + '"]')
            .checked = true;
        $('export-view-stop-data').disabled = false;
      } else if (exportNetLogInfo.state == 'UNINITIALIZED') {
        $('export-view-file-path-text').textContent =
            'Unable to initialize NetLog data file.';
      }
    },

    /*
     * Returns true if the UI is being displayed for mobile, otherwise false
     * for desktop. This is controlled by the HTML template.
     */
    useMobileUI_: function() {
      return !!document.getElementById('export-view-mobile-send-data');
    }
  };

  return NetExportView;
})();
