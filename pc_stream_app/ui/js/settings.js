/**
 * HT306 Home Theater - Settings, Driver Management, and Lip-Sync Timing
 * Handles audio capture devices, format modes, lip-sync delay alignment, and VB-CABLE driver actions.
 */
(function () {
  "use strict";

  // --- ENSURE GLOBAL DRIVER STATE ---
  window.audioDriverState = window.audioDriverState || { installed: false, is_default: false, name: "", debug_override: false };

  // --- FORMAT MODE SELECTOR ---
  window.setFormatModeUi = function (modeIdx) {
    const formatModeBtns = document.querySelectorAll(".format-mode-btn");
    formatModeBtns.forEach((btn) => {
      const idx = parseInt(btn.getAttribute("data-format"), 10);
      if (idx === modeIdx) {
        btn.className = "format-mode-btn px-space-sm py-0.5 rounded-full bg-primary-container text-text-primary font-caption text-caption font-medium shadow-sm transition-all";
      } else {
        btn.className = "format-mode-btn px-space-sm py-0.5 rounded-full text-text-secondary hover:text-text-primary font-caption text-caption transition-all";
      }
    });
  };

  // --- LIP-SYNC TIMING & DELAY ALIGNMENT ---
  window.updateDelayUi = function (ms) {
    const num = parseInt(ms, 10);
    const lipsyncSlider = document.getElementById("lipsync-slider");
    const lipsyncDisplay = document.getElementById("lipsync-display");
    const delayBlock = document.getElementById("timeline-delay-block");
    const lipsyncSubframes = document.getElementById("lipsync-subframes");

    if (lipsyncSlider) lipsyncSlider.value = num;
    if (lipsyncDisplay) lipsyncDisplay.textContent = num + " ms";
    if (delayBlock) delayBlock.style.width = Math.max(8, (num / 200) * 100) + "%";

    if (lipsyncSubframes) {
      const subframes = Math.round((num / 1000) * 96000 / 288);
      const frames60 = ((num / 1000) * 60).toFixed(2);
      lipsyncSubframes.textContent = `${subframes} subframes / ${frames60} frames @ 60Hz`;
    }

    document.querySelectorAll(".delay-pill").forEach((pill) => {
      const v = parseInt(pill.getAttribute("data-val"), 10);
      if (v === num) {
        pill.className = "delay-pill px-space-md py-0.5 rounded-full bg-primary-container text-text-primary font-code-sm text-code-sm font-semibold transition-colors";
      } else {
        pill.className = "delay-pill px-space-md py-0.5 rounded-full bg-surface-2 hover:bg-surface-variant font-code-sm text-code-sm text-text-secondary transition-colors";
      }
    });
  };

  window.setDelay = function (ms) {
    const num = parseInt(ms, 10);
    window.updateDelayUi(num);
    window.sendNativeCommand("set_lipsync", { lip_sync_delay_ms: num });
    if (typeof window.sendConfigUpdate === "function") {
      window.sendConfigUpdate({ lip_sync_delay_ms: num });
    }
  };

  window.adjustDelay = function (delta) {
    const lipsyncSlider = document.getElementById("lipsync-slider");
    const cur = lipsyncSlider ? parseInt(lipsyncSlider.value, 10) : 0;
    const next = Math.max(0, Math.min(200, cur + delta));
    window.setDelay(next);
  };

  // --- AUDIO DRIVER INSTALLER & TASKBAR SWITCHING ---
  let isDriverActionBusy = false;

  function showDriverInstallingState() {
    const audioText = document.getElementById("header-audio-text");
    const audioDot = document.getElementById("header-audio-dot");
    const settingDriverStatus = document.getElementById("setting-driver-status");
    const btnInstallDriver = document.getElementById("btn-install-vbcable");

    if (audioText) {
      audioText.innerText = "Membuka Installer...";
      audioText.className = "font-code-sm text-code-sm text-status-warn font-medium animate-pulse";
    }
    if (audioDot) audioDot.className = "w-2 h-2 rounded-full bg-status-warn animate-pulse";
    if (settingDriverStatus) {
      settingDriverStatus.innerHTML = `<span class="text-status-warn font-medium animate-pulse">Menunggu persetujuan UAC & dialog setup...</span>`;
    }
    if (btnInstallDriver) {
      btnInstallDriver.innerHTML = `<span class="material-symbols-outlined text-[16px] animate-spin">sync</span><span>Membuka Setup...</span>`;
      btnInstallDriver.disabled = true;
      setTimeout(() => { if (btnInstallDriver) btnInstallDriver.disabled = false; }, 15000);
    }
  }

  window.handleDebugAudioToggle = function (e) {
    if (e && e.stopPropagation) e.stopPropagation();
    const nextEnabled = !(window.audioDriverState && window.audioDriverState.debug_override);
    if (window.audioDriverState) {
      window.audioDriverState.debug_override = nextEnabled;
    }
    window.sendNativeCommand("toggle_debug_audio", { enabled: nextEnabled });
  };

  window.handleAudioBadgeClick = function (e) {
    if (e) {
      if (e.stopPropagation) e.stopPropagation();
      if (e.preventDefault) e.preventDefault();
    }
    if (isDriverActionBusy) return;
    isDriverActionBusy = true;
    setTimeout(() => { isDriverActionBusy = false; }, 800);

    if (!window.audioDriverState || !window.audioDriverState.installed) {
      showDriverInstallingState();
      window.sendNativeCommand("install_audio_driver", { driver: "vbcable" });
    } else if (window.audioDriverState.name && window.audioDriverState.name.includes("CABLE")) {
      showDriverInstallingState();
      window.sendNativeCommand("sync_audio_names", {});
    } else if (window.audioDriverState.is_default) {
      window.sendNativeCommand("restore_default_playback", {});
    } else {
      window.sendNativeCommand("switch_to_ht306", {});
    }
  };

  window.handleInstallCableClick = function (e) {
    if (e) {
      if (e.stopPropagation) e.stopPropagation();
      if (e.preventDefault) e.preventDefault();
    }
    if (isDriverActionBusy) return;
    isDriverActionBusy = true;
    setTimeout(() => { isDriverActionBusy = false; }, 800);

    if (window.audioDriverState && window.audioDriverState.installed) {
      if (window.audioDriverState.name && window.audioDriverState.name.includes("CABLE")) {
        showDriverInstallingState();
        window.sendNativeCommand("sync_audio_names", {});
      } else if (window.audioDriverState.is_default) {
        window.sendNativeCommand("restore_default_playback", {});
      } else {
        window.sendNativeCommand("switch_to_ht306", {});
      }
    } else {
      showDriverInstallingState();
      window.sendNativeCommand("install_audio_driver", { driver: "vbcable" });
    }
  };

  function initSettings() {
    // Audio Capture Device Selector
    const devSelect = document.getElementById("setting-device-select");
    if (devSelect) {
      devSelect.addEventListener("change", (e) => {
        const idx = parseInt(e.target.value, 10);
        window.sendNativeCommand("set_device", { device_index: idx });
        if (typeof window.sendConfigUpdate === "function") {
          window.sendConfigUpdate({ active_device_index: idx });
        }
      });
    }

    // Format Mode buttons
    const formatModeBtns = document.querySelectorAll(".format-mode-btn");
    formatModeBtns.forEach((btn) => {
      btn.addEventListener("click", () => {
        const idx = parseInt(btn.getAttribute("data-format"), 10);
        window.setFormatModeUi(idx);
        window.sendNativeCommand("set_format_mode", { format_mode: idx });
        if (typeof window.sendConfigUpdate === "function") {
          window.sendConfigUpdate({ format_mode: idx });
        }
      });
    });

    // Lipsync slider
    const lipsyncSlider = document.getElementById("lipsync-slider");
    if (lipsyncSlider) {
      lipsyncSlider.addEventListener("input", (e) => {
        window.setDelay(e.target.value);
      });
    }

    // Header audio badge click
    const headerAudioBadge = document.getElementById("header-audio-badge");
    if (headerAudioBadge) {
      headerAudioBadge.addEventListener("click", window.handleAudioBadgeClick);
    }

    // Install Cable button
    const installCableBtn = document.getElementById("btn-install-vbcable");
    if (installCableBtn) {
      installCableBtn.addEventListener("click", window.handleInstallCableClick);
    }

    // Settings Debug Audio Toggle Checkbox
    const settingDebugToggle = document.getElementById("setting-debug-audio-toggle");
    if (settingDebugToggle) {
      settingDebugToggle.addEventListener("change", window.handleDebugAudioToggle);
    }

    // BOOTSEL Mode Reboot Button
    const rebootBootselBtn = document.getElementById("reboot-bootsel-btn");
    if (rebootBootselBtn) {
      rebootBootselBtn.addEventListener("click", () => {
        if (confirm("Reboot RP2350-Zero ke mode ROM BOOTSEL untuk flashing UF2?")) {
          const sent = window.sendNativeCommand("reboot_bootsel", { target: "RP2350-Zero" });
          if (!sent) {
            alert("Perintah BOOTSEL dikirim ke controller.");
          }
        }
      });
    }
  }

  window.initSettings = initSettings;

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", initSettings);
  } else {
    initSettings();
  }
})();
