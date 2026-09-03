(() => {
  if (window.__qingyingLongshotContentInstalled) {
    return;
  }
  window.__qingyingLongshotContentInstalled = true;

  const state = {
    originalLeft: 0,
    originalTop: 0,
    originalScrollBehavior: "",
    prepared: false,
    controls: null
  };

  function removeControls() {
    state.controls?.remove();
    state.controls = null;
  }

  function showControls(paused, frames = 0) {
    removeControls();
    const panel = document.createElement("div");
    panel.style.cssText = "position:fixed;right:24px;bottom:24px;z-index:2147483647;display:flex;gap:8px;align-items:center;padding:10px 12px;border-radius:9px;background:#1f2937;color:#fff;font:13px Microsoft YaHei UI,Segoe UI,sans-serif;box-shadow:0 8px 24px rgba(0,0,0,.3);";
    const status = document.createElement("span");
    status.textContent = `\u957f\u622a\u56fe\uff1a${paused ? "\u5df2\u6682\u505c" : "\u8fdb\u884c\u4e2d"} ${frames ? `(${frames}\u5c4f)` : ""}`;
    const pause = document.createElement("button");
    pause.textContent = paused ? "\u7ee7\u7eed" : "\u6682\u505c";
    const stop = document.createElement("button");
    stop.textContent = "\u505c\u6b62";
    for (const button of [pause, stop]) button.style.cssText = "border:0;border-radius:5px;padding:6px 10px;color:#fff;cursor:pointer;font:inherit;background:#2563eb;";
    stop.style.background = "#dc2626";
    pause.onclick = () => chrome.runtime.sendMessage({ type: "QINGYING_LONGSHOT_CONTROL", control: paused ? "resume" : "pause" });
    stop.onclick = () => chrome.runtime.sendMessage({ type: "QINGYING_LONGSHOT_CONTROL", control: "stop" });
    panel.append(status, pause, stop);
    document.documentElement.appendChild(panel);
    state.controls = panel;
  }

  function scrollingElement() {
    return document.scrollingElement || document.documentElement;
  }

  function metrics() {
    const root = scrollingElement();
    return {
      documentHeight: Math.max(root.scrollHeight, document.documentElement.scrollHeight, document.body?.scrollHeight || 0),
      documentWidth: Math.max(root.scrollWidth, document.documentElement.scrollWidth, document.body?.scrollWidth || 0),
      viewportHeight: window.innerHeight,
      viewportWidth: window.innerWidth,
      scrollTop: window.scrollY,
      devicePixelRatio: window.devicePixelRatio || 1
    };
  }

  function afterLayout() {
    return new Promise((resolve) => requestAnimationFrame(() => requestAnimationFrame(resolve)));
  }

  // Keep the page movement visually consistent with QingYing's native
  // long-shot flow: advance one selected-height segment through animation,
  // rather than jumping to the next screenshot position in one frame.
  function scrollSmoothlyTo(targetTop) {
    const startTop = window.scrollY;
    const distance = targetTop - startTop;
    if (Math.abs(distance) < 1) return Promise.resolve();
    // Keep each visibly selected-height advance slow enough for the user to
    // inspect it and press Pause/Stop, matching QingYing's interactive path.
    const duration = Math.max(1500, Math.min(2600, Math.abs(distance) * 4));
    const startedAt = performance.now();
    return new Promise((resolve) => {
      const tick = (now) => {
        const progress = Math.min(1, (now - startedAt) / duration);
        const eased = 1 - Math.pow(1 - progress, 3);
        window.scrollTo(0, Math.round(startTop + distance * eased));
        if (progress < 1) requestAnimationFrame(tick);
        else resolve();
      };
      requestAnimationFrame(tick);
    });
  }

  chrome.runtime.onMessage.addListener((message, _sender, sendResponse) => {
    if (message?.type === "QINGYING_LONGSHOT_PREPARE") {
      state.originalLeft = window.scrollX;
      state.originalTop = window.scrollY;
      state.originalScrollBehavior = document.documentElement.style.scrollBehavior;
      document.documentElement.style.scrollBehavior = "auto";
      state.prepared = true;
      void afterLayout().then(() => sendResponse({ ok: true, ...metrics() }));
      return true;
    }

    if (message?.type === "QINGYING_LONGSHOT_SHOW_CONTROLS" || message?.type === "QINGYING_LONGSHOT_UPDATE_CONTROLS") {
      showControls(Boolean(message.paused), Number(message.frames) || 0);
      sendResponse({ ok: true });
      return false;
    }
    if (message?.type === "QINGYING_LONGSHOT_HIDE_CONTROLS") {
      removeControls();
      sendResponse({ ok: true });
      return false;
    }

    if (message?.type === "QINGYING_LONGSHOT_SELECT_REGION") {
      const shade = document.createElement("div");
      shade.style.cssText = "position:fixed;inset:0;z-index:2147483647;cursor:crosshair;background:rgba(0,0,0,.18);";
      const box = document.createElement("div");
      box.style.cssText = "position:fixed;display:none;border:2px solid #2563eb;background:rgba(37,99,235,.12);pointer-events:none;";
      shade.appendChild(box);
      document.documentElement.appendChild(shade);
      let startX = 0;
      let startY = 0;
      const cleanup = () => shade.remove();
      shade.addEventListener("mousedown", (event) => {
        startX = event.clientX;
        startY = event.clientY;
        box.style.display = "block";
      });
      shade.addEventListener("mousemove", (event) => {
        if (box.style.display === "none") return;
        const left = Math.min(startX, event.clientX);
        const top = Math.min(startY, event.clientY);
        box.style.left = `${left}px`;
        box.style.top = `${top}px`;
        box.style.width = `${Math.abs(event.clientX - startX)}px`;
        box.style.height = `${Math.abs(event.clientY - startY)}px`;
      });
      shade.addEventListener("mouseup", (event) => {
        const left = Math.max(0, Math.min(startX, event.clientX));
        const top = Math.max(0, Math.min(startY, event.clientY));
        const width = Math.abs(event.clientX - startX);
        const height = Math.abs(event.clientY - startY);
        cleanup();
        if (width < 32 || height < 32) {
          sendResponse({ ok: false, error: "选区至少需要 32 × 32 像素" });
          return;
        }
        sendResponse({ ok: true, left, top, width, height, scrollTop: window.scrollY });
      }, { once: true });
      shade.addEventListener("contextmenu", (event) => {
        event.preventDefault();
        cleanup();
        sendResponse({ ok: false, error: "已取消框选" });
      }, { once: true });
      return true;
    }

    if (message?.type === "QINGYING_LONGSHOT_SCROLL") {
      if (!state.prepared) {
        sendResponse({ ok: false, error: "\u957f\u622a\u56fe\u5c1a\u672a\u521d\u59cb\u5316" });
        return false;
      }
      void scrollSmoothlyTo(Math.max(0, Number(message.top) || 0)).then(() => afterLayout()).then(() => {
        sendResponse({ ok: true, top: window.scrollY, ...metrics() });
      });
      return true;
    }

    if (message?.type === "QINGYING_LONGSHOT_RESTORE") {
      if (state.prepared) {
        window.scrollTo(state.originalLeft, state.originalTop);
        document.documentElement.style.scrollBehavior = state.originalScrollBehavior;
        state.prepared = false;
      }
      sendResponse({ ok: true });
      return false;
    }

    return false;
  });
})();
