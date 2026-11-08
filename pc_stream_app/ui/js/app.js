/**
 * HT306 Home Theater - Main Application Coordinator & Lifecycle
 * Manages tab switching, window dragging/closing, logs terminal, stream toggle, and native app_ready handshake.
 */
(function () {
  "use strict";

  // --- ENSURE APP STATE ---
  window.currentTab = window.currentTab || "dashboard";
  window.currentMode = window.currentMode || "desktop";
  window.isStreaming = window.isStreaming !== undefined ? window.isStreaming : true;
  window.logBuffer = window.logBuffer || [];

  let currentLogFilter = "all";

  // --- TAB NAVIGATION ENGINE ---
  window.switchTab = function (tabName) {
    window.currentTab = tabName;
    const navBtns = document.querySelectorAll("#sidebar-nav .nav-btn");
    navBtns.forEach((btn) => {
      const tab = btn.getAttribute("data-tab");
      if (tab === tabName) {
        btn.className = "nav-btn w-full text-left flex items-center gap-space-sm px-space-md py-space-sm transition-colors bg-primary-container text-text-primary font-medium border-l-2 border-primary-container rounded-lg";
      } else {
        btn.className = "nav-btn w-full text-left flex items-center gap-space-sm px-space-md py-space-sm rounded-lg text-text-secondary hover:bg-surface-2 hover:text-on-surface transition-colors font-body-sm text-body-sm";
      }
    });

    const panels = document.querySelectorAll(".tab-panel");
    panels.forEach((p) => {
      if (p.id === "tab-" + tabName) {
        p.classList.remove("hidden");
      } else {
        p.classList.add("hidden");
      }
    });

    window.sendNativeCommand("tab_switched", { tab: tabName });

    if (tabName === "log" && !window.isNativeWebView2) {
      fetchLogs();
    }
  };

  // --- LOG & DIAGNOSTICS TERMINAL ---
  function fetchLogs() {
    if (window.isNativeWebView2) return;
    fetch("/api/logs")
      .then((r) => r.json())
      .then((logs) => {
        if (!Array.isArray(logs)) return;
        window.logBuffer = logs;
        renderLogs();
      })
      .catch((err) => console.warn("[HT306 App] Log fetch failed:", err));
  }
  window.fetchLogs = fetchLogs;

  function renderLogs() {
    const terminal = document.getElementById("terminal-stream");
    const logCountBadge = document.getElementById("log-count");
    const searchInput = document.getElementById("log-search-input");
    const autoScroll = document.getElementById("auto-scroll-toggle");

    if (!terminal) return;

    const query = (searchInput ? searchInput.value : "").toLowerCase().trim();
    let visibleCount = 0;

    let html = "";
    (window.logBuffer || []).forEach((line) => {
      const lower = String(line).toLowerCase();
      let level = "info";
      if (lower.includes("error") || lower.includes("fail")) level = "error";
      else if (lower.includes("warn")) level = "warn";
      else if (lower.includes("debug")) level = "debug";

      if (currentLogFilter !== "all" && level !== currentLogFilter) return;
      if (query && !lower.includes(query)) return;

      visibleCount++;

      let levelBadge = '<span class="font-meter-num text-meter-num text-status-ok bg-status-ok/10 px-1 rounded shrink-0">INFO</span>';
      if (level === "error") {
        levelBadge = '<span class="font-meter-num text-meter-num text-status-error bg-status-error/10 px-1 rounded shrink-0">ERROR</span>';
      } else if (level === "warn") {
        levelBadge = '<span class="font-meter-num text-meter-num text-status-warn bg-status-warn/10 px-1 rounded shrink-0">WARN</span>';
      } else if (level === "debug") {
        levelBadge = '<span class="font-meter-num text-meter-num text-status-info bg-status-info/10 px-1 rounded shrink-0">DEBUG</span>';
      }

      const escaped = typeof window.escapeHtml === "function" ? window.escapeHtml(line) : String(line);
      html += `
        <div class="log-line group flex items-start gap-space-sm hover:bg-surface-2/40 px-1 py-0.5 rounded transition-colors select-text">
          <span class="text-icon-gray font-meter-num text-meter-num select-none w-24 shrink-0">[${new Date().toLocaleTimeString()}]</span>
          ${levelBadge}
          <span class="text-on-surface font-code-sm text-code-sm">${escaped}</span>
        </div>
      `;
    });

    terminal.innerHTML = html || '<div class="text-icon-gray font-code-sm p-4 text-center">Tidak ada entri log yang cocok dengan filter.</div>';
    if (logCountBadge) logCountBadge.textContent = visibleCount;
    if (autoScroll && autoScroll.checked) {
      terminal.scrollTop = terminal.scrollHeight;
    }
  }
  window.renderLogs = renderLogs;

  function initApp() {
    // --- NATIVE WINDOW DRAG LISTENER ---
    const appHeader = document.querySelector("header");
    if (appHeader) {
      appHeader.addEventListener("mousedown", (e) => {
        if (!e.target.closest("button, input, select, a, [onclick], [data-tab], #header-audio-badge, #btn-install-vbcable")) {
          window.sendNativeCommand("window_drag");
        }
      });
    }

    // --- SIDEBAR TAB BUTTONS ---
    document.querySelectorAll("#sidebar-nav .nav-btn").forEach((btn) => {
      btn.addEventListener("click", () => {
        const tab = btn.getAttribute("data-tab");
        if (tab) window.switchTab(tab);
      });
    });

    // --- CROSS-TAB NAVIGATION JUMP LINKS ---
    document.addEventListener("click", (e) => {
      const sweepJump = e.target.closest("#dash-sweep-btn, #sweep-btn, [data-nav-tab='test']");
      if (sweepJump) {
        e.preventDefault();
        window.switchTab("test");
      }
      const routingJump = e.target.closest("[data-nav-tab='routing']");
      if (routingJump) {
        e.preventDefault();
        window.switchTab("routing");
      }
    });

    // --- STREAM TOGGLE BUTTON ---
    const streamBtn = document.getElementById("toggle-stream-btn");
    if (streamBtn) {
      streamBtn.addEventListener("click", () => {
        window.isStreaming = !window.isStreaming;
        if (typeof window.updateStreamButtonUi === "function") {
          window.updateStreamButtonUi(window.isStreaming);
        }
        window.sendNativeCommand("toggle_stream", { streaming: window.isStreaming });
        if (typeof window.sendConfigUpdate === "function") {
          window.sendConfigUpdate({ state: window.isStreaming ? "STREAMING" : "CONNECTED" });
        }
      });
    }

    // --- MODE SWITCH BUTTONS ---
    const desktopBtn = document.getElementById("mode-desktop-btn");
    if (desktopBtn) {
      desktopBtn.addEventListener("click", () => {
        window.currentMode = "desktop";
        if (typeof window.updateModeUi === "function") {
          window.updateModeUi("desktop");
        }
        window.sendNativeCommand("set_mode", { mode: "desktop", bit_perfect_mode: false });
        if (typeof window.sendConfigUpdate === "function") {
          window.sendConfigUpdate({ mode: "desktop", bit_perfect_mode: false });
        }
      });
    }

    const passBtn = document.getElementById("mode-passthrough-btn");
    if (passBtn) {
      passBtn.addEventListener("click", () => {
        window.currentMode = "bitperfect";
        if (typeof window.updateModeUi === "function") {
          window.updateModeUi("bitperfect");
        }
        window.sendNativeCommand("set_mode", { mode: "bitperfect", bit_perfect_mode: true });
        if (typeof window.sendConfigUpdate === "function") {
          window.sendConfigUpdate({ mode: "bitperfect", bit_perfect_mode: true });
        }
      });
    }

    // --- RESET BUFFER BUTTON (DASHBOARD) ---
    const resetBufferBtn = document.getElementById("btn-reset-buffer");
    if (resetBufferBtn) {
      resetBufferBtn.addEventListener("click", () => {
        window.sendNativeCommand("reset_buffer", {});
        const originalText = resetBufferBtn.innerHTML;
        resetBufferBtn.innerHTML = '<span class="material-symbols-outlined text-[13px] text-status-ok">check</span><span>Reset!</span>';
        setTimeout(() => { resetBufferBtn.innerHTML = originalText; }, 1500);
      });
    }

    // --- LOG FILTER CHIPS & CONTROLS ---
    document.querySelectorAll(".filter-chip").forEach((chip) => {
      chip.addEventListener("click", () => {
        document.querySelectorAll(".filter-chip").forEach((c) => {
          c.classList.remove("bg-surface-variant", "text-text-primary");
          c.classList.add("bg-surface-2", "text-text-secondary");
        });
        chip.classList.remove("bg-surface-2", "text-text-secondary");
        chip.classList.add("bg-surface-variant", "text-text-primary");
        currentLogFilter = chip.getAttribute("data-filter") || "all";
        renderLogs();
      });
    });

    const logSearch = document.getElementById("log-search-input");
    if (logSearch) logSearch.addEventListener("input", renderLogs);

    const clearBufferBtn = document.getElementById("btn-clear-buffer");
    if (clearBufferBtn) {
      clearBufferBtn.addEventListener("click", () => {
        window.logBuffer = [];
        renderLogs();
        window.sendNativeCommand("clear_logs", {});
        window.sendNativeCommand("reset_buffer", {});
      });
    }

    const copyLogBtn = document.getElementById("btn-copy-log");
    if (copyLogBtn) {
      copyLogBtn.addEventListener("click", () => {
        const text = (window.logBuffer || []).join("\n");
        if (navigator.clipboard) {
          navigator.clipboard.writeText(text).then(() => {
            const orig = copyLogBtn.innerHTML;
            copyLogBtn.innerHTML = '<span class="material-symbols-outlined text-[16px] text-status-ok">check</span><span>Copied!</span>';
            setTimeout(() => { copyLogBtn.innerHTML = orig; }, 1800);
          });
        }
      });
    }

    if (!window.isNativeWebView2 && window.location.protocol.startsWith("http")) {
      setInterval(() => {
        if (window.currentTab === "log") fetchLogs();
      }, 1500);
    }

    // Free Tailwind JIT compiler runtime post-load
    window.addEventListener("load", () => {
      setTimeout(() => {
        try {
          if (window.tailwind) {
            delete window.tailwind;
          }
        } catch (_) {
          window.tailwind = undefined;
        }
        window.sendNativeCommand("app_ready", { version: "1.3.0", event: "post_load" });
      }, 400);
    });

    // Initial Tab Navigation View
    window.switchTab("dashboard");
  }

  window.initApp = initApp;

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", initApp);
  } else {
    initApp();
  }
})();
