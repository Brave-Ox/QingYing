const MAX_CAPTURED_FRAMES = 100;
const MAX_OUTPUT_PIXELS = 40 * 1024 * 1024;
// Chrome limits captureVisibleTab calls to roughly two calls per second.
const SETTLE_DELAY_MS = 550;

let isCapturing = false;

function sleep(milliseconds) {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}

async function updateStatus(state, message) {
  await chrome.storage.local.set({
    longshotStatus: { state, message, updatedAt: Date.now() }
  });
}

function sendTabMessage(tabId, message) {
  return chrome.tabs.sendMessage(tabId, message);
}

function formatFileName(title) {
  const safeTitle = (title || "\u7f51\u9875\u957f\u622a\u56fe")
    .replace(/[\\/:*?"<>|]/g, "_")
    .replace(/\s+/g, " ")
    .trim()
    .slice(0, 80) || "\u7f51\u9875\u957f\u622a\u56fe";
  const timestamp = new Date().toISOString().replace(/[:.]/g, "-");
  return `QingYing/${safeTitle}-${timestamp}.png`;
}

async function captureVisible(windowId) {
  return chrome.tabs.captureVisibleTab(windowId, { format: "png" });
}

async function dataUrlToBitmap(dataUrl) {
  const response = await fetch(dataUrl);
  return createImageBitmap(await response.blob());
}

function blobToDataUrl(blob) {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.addEventListener("load", () => resolve(reader.result));
    reader.addEventListener("error", () => reject(reader.error));
    reader.readAsDataURL(blob);
  });
}

async function stitchAndDownload(frames, selection, title) {
  if (frames.length === 0) {
    throw new Error("\u6ca1\u6709\u83b7\u5f97\u4efb\u4f55\u7f51\u9875\u753b\u9762");
  }

  const firstBitmap = await dataUrlToBitmap(frames[0].dataUrl);
  const scale = firstBitmap.width / frames[0].viewportWidth;
  const outputWidth = Math.round(selection.width * scale);
  const outputHeight = Math.round(selection.height * scale) * frames.length;
  firstBitmap.close();

  if (outputWidth * outputHeight > MAX_OUTPUT_PIXELS) {
    throw new Error("\u7f51\u9875\u8fc7\u957f\uff0c\u5bfc\u51fa\u50cf\u7d20\u8d85\u8fc7 4000 \u4e07\uff1b\u8bf7\u7f29\u5c0f\u9875\u9762\u6216\u5206\u6bb5\u622a\u56fe");
  }

  const canvas = new OffscreenCanvas(outputWidth, outputHeight);
  const context = canvas.getContext("2d", { alpha: false });
  const sourceLeft = Math.round(selection.left * scale);
  const sourceTop = Math.round(selection.top * scale);
  const sourceHeight = Math.round(selection.height * scale);
  for (let index = 0; index < frames.length; ++index) {
    const frame = frames[index];
    const bitmap = await dataUrlToBitmap(frame.dataUrl);
    context.drawImage(bitmap, sourceLeft, sourceTop, outputWidth, sourceHeight,
      0, index * sourceHeight, outputWidth, sourceHeight);
    bitmap.close();
  }

  const blob = await canvas.convertToBlob({ type: "image/png" });
  await chrome.downloads.download({
    url: await blobToDataUrl(blob),
    filename: formatFileName(title),
    saveAs: true,
    conflictAction: "uniquify"
  });
}

async function startLongshot() {
  if (isCapturing) {
    throw new Error("\u5df2\u6709\u957f\u622a\u56fe\u6b63\u5728\u8fdb\u884c");
  }
  isCapturing = true;

  let tab;
  let prepared = false;
  try {
    [tab] = await chrome.tabs.query({ active: true, lastFocusedWindow: true });
    if (!tab?.id || !tab.windowId || !tab.url || /^(chrome|edge|about):\/\//i.test(tab.url)) {
      throw new Error("\u8bf7\u5207\u6362\u5230\u666e\u901a\u7f51\u9875\u540e\u518d\u4f7f\u7528\u957f\u622a\u56fe");
    }

    await chrome.scripting.executeScript({ target: { tabId: tab.id }, files: ["content.js"] });
    const selection = await sendTabMessage(tab.id, { type: "QINGYING_LONGSHOT_SELECT_REGION" });
    if (!selection?.ok) throw new Error(selection?.error || "未完成选区框选");
    await updateStatus("capturing", "已框选区域，正在截取第一屏…");
    const page = await sendTabMessage(tab.id, { type: "QINGYING_LONGSHOT_PREPARE" });
    if (!page?.ok || page.viewportHeight <= 0 || page.viewportWidth <= 0) {
      throw new Error(page?.error || "\u65e0\u6cd5\u8bfb\u53d6\u5f53\u524d\u7f51\u9875\u5c3a\u5bf8");
    }
    prepared = true;

    const maxScrollTop = Math.max(0, page.documentHeight - page.viewportHeight);
    const frames = [];
    let requestedTop = selection.scrollTop;
    let previousTop = -1;

    while (frames.length < MAX_CAPTURED_FRAMES) {
      const position = await sendTabMessage(tab.id, {
        type: "QINGYING_LONGSHOT_SCROLL",
        top: requestedTop
      });
      if (!position?.ok) {
        throw new Error(position?.error || "\u7f51\u9875\u6eda\u52a8\u5931\u8d25");
      }
      await sleep(SETTLE_DELAY_MS);

      const dataUrl = await captureVisible(tab.windowId);
      frames.push({
        dataUrl,
        top: position.top,
        viewportWidth: position.viewportWidth
      });
      await updateStatus("capturing", `\u6b63\u5728\u622a\u53d6\u7b2c ${frames.length} \u5c4f\u2026`);

      if (position.top >= maxScrollTop || position.top === previousTop) {
        break;
      }
      previousTop = position.top;
      requestedTop = Math.min(maxScrollTop, position.top + selection.height);
    }

    if (frames.length >= MAX_CAPTURED_FRAMES && frames.at(-1).top < maxScrollTop) {
      throw new Error("\u7f51\u9875\u8d85\u8fc7 100 \u5c4f\uff0c\u8bf7\u7f29\u5c0f\u9875\u6216\u5206\u6bb5\u622a\u56fe");
    }

    await updateStatus("stitching", "\u6b63\u5728\u62fc\u63a5\u5e76\u751f\u6210 PNG\u2026");
    await stitchAndDownload(frames, selection, tab.title);
    await updateStatus("complete", `\u5df2\u5b8c\u6210\uff1a\u5171 ${frames.length} \u5c4f\uff0cPNG \u5df2\u8fdb\u5165\u4fdd\u5b58\u6d41\u7a0b`);
  } catch (error) {
    await updateStatus("error", `\u957f\u622a\u56fe\u5931\u8d25\uff1a${error.message}`);
    throw error;
  } finally {
    if (prepared && tab?.id) {
      try {
        await sendTabMessage(tab.id, { type: "QINGYING_LONGSHOT_RESTORE" });
      } catch {
        // Page navigation or closing prevents restoration; retain the original error.
      }
    }
    isCapturing = false;
  }
}

chrome.runtime.onMessage.addListener((message, _sender, sendResponse) => {
  if (message?.type !== "QINGYING_START_LONGSHOT") {
    return false;
  }
  void startLongshot().catch((error) => console.error("QingYing longshot failed", error));
  sendResponse({ ok: true });
  return false;
});

void updateStatus("idle", "\u51c6\u5907\u5c31\u7eea");
