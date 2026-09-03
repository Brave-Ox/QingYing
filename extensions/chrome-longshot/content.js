(() => {
  if (window.__qingyingLongshotContentInstalled) {
    return;
  }
  window.__qingyingLongshotContentInstalled = true;

  const state = {
    originalLeft: 0,
    originalTop: 0,
    originalScrollBehavior: "",
    prepared: false
  };

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
      window.scrollTo(0, Math.max(0, Number(message.top) || 0));
      void afterLayout().then(() => {
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
