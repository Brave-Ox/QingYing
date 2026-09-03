# QingYing Chrome long-shot extension

This is an independent Chrome Manifest V3 extension. It reads the page height, scrolls the active tab, captures each viewport through Chrome, then stitches the frames into a PNG download. It complements (rather than replaces) the native DLL long-shot path in `qingying.exe`.

## Load unpacked for testing

1. Build QingYing. The extension is deployed to `build/bin/Release/chrome-extension`; using this source directory also works.
2. Open `chrome://extensions`, turn on Developer mode, then choose Load unpacked.
3. Select the `chrome-extension` directory.
4. Copy the extension ID shown on the Chrome extension card, then run `install-native-host.ps1 -ExtensionId <ID>` from PowerShell.
5. Open a normal webpage, click the QingYing extension icon, and start the capture.
6. On completion, the page scroll position is restored; the image is sent to QingYing for copy, save, annotation and pin actions.

For the local `outputs/chrome_longshot_fixture.html` fixture, first open the extension Details page and enable “Allow access to file URLs”.

## Limits

- Chrome internal/settings pages cannot be captured.
- One capture is limited to 100 viewports and 40 million output pixels.
- Fixed elements, animations, videos, and lazy-loaded content are rendered according to the live page state. A fixed header may repeat at a frame boundary.
- This is a browser-native export. It does not yet return the PNG to QingYing's clipboard, annotation, or pin workflow. That requires a later Native Messaging Host integration.

## Suggested acceptance checks

1. Open `outputs/chrome_longshot_fixture.html`, capture it, and verify the PNG is taller than one viewport with continuous sections.
2. Verify the page returns to its original scroll position.
3. Verify an over-limit page reports an error instead of freezing Chrome.
4. On `chrome://settings`, verify the extension reports that only ordinary webpages are supported.
