# Quick Start: Set Up VII Speedrun Patch

Prepare Game unpacks assets and builds a texture cache before you play, so the
first setup can take a while.

## Before you start

- Allow roughly 35 GB of free space for prepared data, with room for the cache
  to grow. You can choose another drive using **Cache folder...** and
  **Unpacked assets...**.
- Close Megadimension Neptunia VII if it is open.
- Leave the game installed where Steam put it. You do not need to move or edit
  game files yourself.

## Set it up

1. Find the ZIP file you downloaded. Right-click it, choose **Extract All...**,
   and extract it somewhere easy to find, such as your Downloads folder or
   Desktop. Do not extract it into the game folder.
2. Open the extracted folder and double-click **VII-Prepare-Game.exe**.
3. If no game folder is shown, open Steam, right-click **Megadimension
   Neptunia VII** in your Library, choose **Manage**, then **Browse local
   files**. In VII Prepare Game, choose **Game folder...** and select the
   folder Steam opened. It is the folder that contains `NeptuniaVII.exe`.
4. Choose **Prepare / Resume**. Leave the computer on and wait for the message
   **Preparation complete**. It is normal for this to take some time.
5. Start the game from Steam as usual.

VII Prepare Game keeps the original game files and handles the setup for you.

## When updating later

Close the game, open the newer **VII-Prepare-Game.exe**, check the displayed
settings and choose **Install / Update**. This saves the patch and settings
without repeating the full preparation. Gameplay options are off by default.

## If you need to stop

Choose **Cancel**. You can run **VII-Prepare-Game.exe** later and choose
**Prepare / Resume** to carry on from the completed work.

## Troubleshooting

- If you run out of space, free some space on the selected drive and choose
  **Prepare / Resume** again. See the user guide before changing data folders;
  choosing a new folder does not move completed work.
- If VII Prepare Game reports a conflicting `dinput8.dll`, it may belong to
  another mod. Resolve that conflict before installing; do not overwrite it
  by hand.

For help, [report the problem on GitHub](https://github.com/Dabomstew/vii-qol-patch/issues)
with the error message and steps that led to it.

For updates, troubleshooting, or advanced options, see the
[full user guide](USER-GUIDE.md).
