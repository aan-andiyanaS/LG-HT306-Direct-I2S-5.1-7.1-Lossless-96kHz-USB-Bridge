/**
 * HT306 Home Theater - Native IPC Bridge Adapter
 * Handles communication between UI and C++ Native Backend (WebView2 & Browser Fallback).
 */
(function () {
  "use strict";

  // --- NATIVE WEBVIEW2 BRIDGE DETECTION ---
  const isNativeWebView2 = !!(window.chrome && window.chrome.webview && typeof window.chrome.webview.postMessage === "function");
  window.isNativeWebView2 = isNativeWebView2;

  /**
   * Send a JSON command message to native C++ backend or WebView2 host.
   * @param {string} action - Action command name (e.g., 'update_config', 'set_mode', 'app_ready')
   * @param {Object} [payload={}] - Additional arguments and parameters
   * @returns {boolean} - True if successfully dispatched to native container
   */
  window.sendNativeCommand = function (action, payload = {}) {
    const message = { ...payload, action };

    // 1. Direct nativePostCommand bridge (Custom C++ wrapper / CEF / Qt / Tauri)
    if (typeof window.nativePostCommand === "function") {
      try {
        window.nativePostCommand(JSON.stringify(message));
        return true;
      } catch (e) {
        console.warn("[HT306 IPC] nativePostCommand failed:", e);
      }
    }

    // 2. Microsoft Edge WebView2 Bridge
    if (isNativeWebView2) {
      try {
        window.chrome.webview.postMessage(message);
        return true;
      } catch (err) {
        console.warn("[HT306 IPC] WebView2 postMessage failed:", err);
      }
    }

    return false;
  };

  /**
   * Helper to send configuration updates to both Native Host and Browser Fallback REST API.
   * @param {Object} payload - Configuration key-value changes
   * @returns {Promise<any>}
   */
  window.sendConfigUpdate = function (payload) {
    window.sendNativeCommand("update_config", payload);
    if (!isNativeWebView2 && window.location.protocol.startsWith("http")) {
      return fetch("/api/config", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload)
      })
        .then((r) => r.json())
        .catch((err) => console.warn("[HT306 IPC] POST /api/config failed:", err));
    }
    return Promise.resolve({ ok: true });
  };

  // --- REGISTER NATIVE WEBVIEW2 INBOUND LISTENER ---
  if (isNativeWebView2) {
    window.chrome.webview.addEventListener("message", (event) => {
      if (typeof window.updateTelemetry === "function") {
        window.updateTelemetry(event.data);
      } else if (typeof window.handleNativeTelemetry === "function") {
        window.handleNativeTelemetry(event.data);
      }
    });
    console.log("[HT306 IPC] Native WebView2 Bridge registered successfully.");
    window.sendNativeCommand("app_ready", { version: "1.3.0" });
  }
})();
