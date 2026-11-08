/**
 * HT306 Home Theater - Audio Profiles & Preferences Manager
 * Manages Movie, Music, Audiophile (Bit-Perfect), and Gaming profiles, and profile save/reset actions.
 */
(function () {
  "use strict";

  // --- ENSURE PROFILE STATE ---
  window.currentProfile = window.currentProfile || "Movie";

  /**
   * Activate an audio profile and update UI indicator banners.
   * @param {string} cardId - DOM element ID of profile card (e.g. 'card-movie')
   * @param {string} name - Display name of profile
   * @param {string} [modeDescription] - Optional mode text (e.g. 'Desktop OS-Linked')
   * @param {number} [delayMs] - Lip-sync delay to automatically configure for this profile
   */
  window.selectProfile = function (cardId, name, modeDescription, delayMs) {
    document.querySelectorAll(".profile-card").forEach((c) => {
      c.classList.remove("bg-surface-container-high", "shadow-md");
      c.classList.add("bg-surface");
    });

    const card = document.getElementById(cardId);
    if (card) {
      card.classList.remove("bg-surface");
      card.classList.add("bg-surface-container-high", "shadow-md");
    }

    const bannerTitle = document.getElementById("active-profile-title");
    if (bannerTitle) bannerTitle.textContent = name;

    window.currentProfile = name;
    const headerProf = document.getElementById("header-profile-text");
    if (headerProf) headerProf.innerText = name;

    // Send native command to backend
    window.sendNativeCommand("set_profile", {
      profile: name,
      card_id: cardId,
      mode_desc: modeDescription,
      delay_ms: delayMs
    });

    if (typeof window.sendConfigUpdate === "function") {
      window.sendConfigUpdate({ profile: name });
    }

    // Auto-apply lipsync delay if specified by profile preset
    if (delayMs !== undefined && typeof window.setDelay === "function") {
      window.setDelay(delayMs);
    }
  };

  function initProfiles() {
    // --- SETTINGS SAVE & PERSIST PREFERENCES ---
    const saveBtn = document.getElementById("saveConfigBtn");
    const toast = document.getElementById("saveToast");
    if (saveBtn && toast) {
      saveBtn.addEventListener("click", () => {
        window.sendNativeCommand("save_config", {});
        toast.classList.remove("translate-y-20", "opacity-0", "pointer-events-none");
        toast.classList.add("translate-y-0", "opacity-100");
        setTimeout(() => {
          toast.classList.remove("translate-y-0", "opacity-100");
          toast.classList.add("translate-y-20", "opacity-0", "pointer-events-none");
        }, 2500);
      });
    }

    // --- FACTORY RESET SETTINGS ---
    const resetBtn = document.getElementById("resetDefaultsBtn");
    if (resetBtn) {
      resetBtn.addEventListener("click", () => {
        if (confirm("Kembalikan konfigurasi buffer audio, tautan perangkat keras, dan hotkey ke pengaturan pabrik?")) {
          window.sendNativeCommand("factory_reset", {});
          location.reload();
        }
      });
    }
  }

  window.initProfiles = initProfiles;

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", initProfiles);
  } else {
    initProfiles();
  }
})();
