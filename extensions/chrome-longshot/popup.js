const captureButton = document.querySelector("#capture");
const statusOutput = document.querySelector("#status");

function renderStatus(status) {
  const text = status?.message ?? "\u51c6\u5907\u5c31\u7eea";
  statusOutput.value = text;
  statusOutput.textContent = text;
  captureButton.disabled = status?.state === "capturing";
}

async function refreshStatus() {
  const { longshotStatus } = await chrome.storage.local.get("longshotStatus");
  renderStatus(longshotStatus);
}

captureButton.addEventListener("click", async () => {
  try {
    await chrome.runtime.sendMessage({ type: "QINGYING_START_LONGSHOT" });
    window.close();
  } catch (error) {
    renderStatus({ state: "error", message: `\u542f\u52a8\u5931\u8d25\uff1a${error.message}` });
  }
});

chrome.storage.onChanged.addListener((changes, areaName) => {
  if (areaName === "local" && changes.longshotStatus) {
    renderStatus(changes.longshotStatus.newValue);
  }
});

void refreshStatus();
