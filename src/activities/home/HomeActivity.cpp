#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Utf8.h>
#include <Xtc.h>

#include <cstdio>
#include <cstring>
#include <vector>

#include "Battery.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/StringUtils.h"

int HomeActivity::getMenuItemCount() const {
  int count = 4;  // My Library, Recents, File transfer, Settings
  if (!recentBooks.empty()) {
    count += recentBooks.size();
  }
  if (hasOpdsUrl) {
    count++;
  }
  return count;
}

void HomeActivity::loadRecentBooks(int maxBooks) {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(std::min(static_cast<int>(books.size()), maxBooks));

  for (const RecentBook& book : books) {
    // Limit to maximum number of recent books
    if (recentBooks.size() >= maxBooks) {
      break;
    }

    // Skip if file no longer exists
    if (!Storage.exists(book.path.c_str())) {
      continue;
    }

    recentBooks.push_back(book);
  }
}

void HomeActivity::loadRecentCovers(int coverHeight) {
  recentsLoading = true;
  bool showingLoading = false;
  Rect popupRect;

  int progress = 0;
  for (RecentBook& book : recentBooks) {
    if (!book.coverBmpPath.empty()) {
      std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
      if (!Storage.exists(coverPath.c_str())) {
        // If epub, try to load the metadata for title/author and cover
        if (StringUtils::checkFileExtension(book.path, ".epub")) {
          Epub epub(book.path, "/.crosspoint");
          // Skip loading css since we only need metadata here
          epub.load(false, true);

          // Try to generate thumbnail image for Continue Reading card
          if (!showingLoading) {
            showingLoading = true;
            popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
          }
          GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
          bool success = epub.generateThumbBmp(coverHeight);
          if (!success) {
            RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
            book.coverBmpPath = "";
          }
          coverRendered = false;
          requestUpdate();
        } else if (StringUtils::checkFileExtension(book.path, ".xtch") ||
                   StringUtils::checkFileExtension(book.path, ".xtc")) {
          // Handle XTC file
          Xtc xtc(book.path, "/.crosspoint");
          if (xtc.load()) {
            // Try to generate thumbnail image for Continue Reading card
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
            bool success = xtc.generateThumbBmp(coverHeight);
            if (!success) {
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
              book.coverBmpPath = "";
            }
            coverRendered = false;
            requestUpdate();
          }
        }
      }
    }
    progress++;
  }

  recentsLoaded = true;
  recentsLoading = false;
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  // Check if OPDS browser URL is configured
  hasOpdsUrl = strlen(SETTINGS.opdsServerUrl) > 0;

  selectorIndex = 0;

  // Load most recent book for "Currently Reading" icon
  loadRecentBooks(1);

  // Generate cover thumbnails for home screen grid
  if (!recentBooks.empty()) {
    loadRecentCovers(44);
  }

  // Trigger first update
  requestUpdate();
}

void HomeActivity::onExit() {
  Activity::onExit();

  // Free the stored cover buffer if any
  freeCoverBuffer();
}

bool HomeActivity::storeCoverBuffer() {
  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  // Free any existing buffer first
  freeCoverBuffer();

  const size_t bufferSize = GfxRenderer::getBufferSize();
  coverBuffer = static_cast<uint8_t*>(malloc(bufferSize));
  if (!coverBuffer) {
    return false;
  }

  memcpy(coverBuffer, frameBuffer, bufferSize);
  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (!coverBuffer) {
    return false;
  }

  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  const size_t bufferSize = GfxRenderer::getBufferSize();
  memcpy(frameBuffer, coverBuffer, bufferSize);
  return true;
}

void HomeActivity::freeCoverBuffer() {
  if (coverBuffer) {
    free(coverBuffer);
    coverBuffer = nullptr;
  }
  coverBufferStored = false;
}

void HomeActivity::loop() {
  const int menuCount = getMenuItemCount();

  buttonNavigator.onNext([this, menuCount] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, menuCount] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    // Calculate dynamic indices based on which options are available
    int idx = 0;
    int menuSelectedIndex = selectorIndex - static_cast<int>(recentBooks.size());
    const int myLibraryIdx = idx++;
    const int recentsIdx = idx++;
    const int opdsLibraryIdx = hasOpdsUrl ? idx++ : -1;
    const int fileTransferIdx = idx++;
    const int settingsIdx = idx;

    if (selectorIndex < recentBooks.size()) {
      onSelectBook(recentBooks[selectorIndex].path);
    } else if (menuSelectedIndex == myLibraryIdx) {
      onMyLibraryOpen();
    } else if (menuSelectedIndex == recentsIdx) {
      onRecentsOpen();
    } else if (menuSelectedIndex == opdsLibraryIdx) {
      onOpdsBrowserOpen();
    } else if (menuSelectedIndex == fileTransferIdx) {
      onFileTransferOpen();
    } else if (menuSelectedIndex == settingsIdx) {
      onSettingsOpen();
    }
  }
}

void HomeActivity::drawMacFolderIcon(int cx, int cy, bool selected) const {
  // Classic Mac OS folder: tab on top-left, rectangular body
  // 48x36 pixels, centered at (cx, cy)
  const int x = cx - 24;
  const int y = cy - 18;
  const bool inv = selected;

  // Folder tab (top-left, extends above body)
  renderer.fillRect(x, y, 20, 10, !inv);
  renderer.fillRect(x + 1, y + 1, 18, 8, inv);

  // Folder body
  renderer.fillRect(x, y + 9, 48, 27, !inv);
  renderer.fillRect(x + 1, y + 10, 46, 25, inv);
}

void HomeActivity::drawMacDocumentIcon(int cx, int cy, bool selected) const {
  const int x = cx - 14;
  const int y = cy - 18;
  const bool inv = selected;

  // Document body
  renderer.fillRect(x, y, 28, 36, !inv);
  renderer.fillRect(x + 1, y + 1, 26, 34, inv);

  // Dog-ear fold (top-right corner)
  renderer.fillRect(x + 21, y + 1, 6, 7, inv);
  renderer.fillRect(x + 20, y + 1, 1, 7, !inv);
  renderer.fillRect(x + 20, y + 7, 7, 1, !inv);

  // Text lines inside document
  renderer.fillRect(x + 4, y + 12, 16, 1, !inv);
  renderer.fillRect(x + 4, y + 17, 16, 1, !inv);
  renderer.fillRect(x + 4, y + 22, 12, 1, !inv);
}

void HomeActivity::drawMacSettingsIcon(int cx, int cy, bool selected) const {
  const bool inv = selected;
  const int x = cx - 18;
  const int w = 36;

  // Three horizontal slider lines with knobs at different positions
  // Slider 1 (top) - knob left
  renderer.fillRect(x, cy - 12, w, 1, !inv);
  renderer.fillRect(x + 6, cy - 15, 6, 7, !inv);
  renderer.fillRect(x + 7, cy - 14, 4, 5, inv);

  // Slider 2 (middle) - knob right
  renderer.fillRect(x, cy - 1, w, 1, !inv);
  renderer.fillRect(x + 22, cy - 4, 6, 7, !inv);
  renderer.fillRect(x + 23, cy - 3, 4, 5, inv);

  // Slider 3 (bottom) - knob center
  renderer.fillRect(x, cy + 10, w, 1, !inv);
  renderer.fillRect(x + 14, cy + 7, 6, 7, !inv);
  renderer.fillRect(x + 15, cy + 8, 4, 5, inv);
}

void HomeActivity::drawMacTransferIcon(int cx, int cy, bool selected) const {
  const bool inv = selected;
  const int leftX = cx - 12;
  constexpr int halfH = 12;
  constexpr int maxW = 28;

  // Right-pointing dart / paper airplane shape
  for (int dy = -halfH; dy <= halfH; dy++) {
    int w = maxW * (halfH - (dy < 0 ? -dy : dy)) / halfH;
    if (w > 0) {
      renderer.fillRect(leftX, cy + dy, w, 1, !inv);
    }
  }

  // Interior
  for (int dy = -(halfH - 1); dy <= (halfH - 1); dy++) {
    int w = (maxW - 2) * (halfH - 1 - (dy < 0 ? -dy : dy)) / (halfH - 1);
    if (w > 0) {
      renderer.fillRect(leftX + 1, cy + dy, w, 1, inv);
    }
  }

  // Fold line through center
  renderer.fillRect(leftX, cy, maxW, 1, !inv);
}

void HomeActivity::drawMacHardDriveIcon(int cx, int cy, bool selected) const {
  const bool inv = selected;
  const int x = cx - 22;
  const int y = cy - 12;

  // Hard drive body
  renderer.fillRect(x, y, 44, 24, !inv);
  renderer.fillRect(x + 1, y + 1, 42, 22, inv);

  // Divider line near bottom
  renderer.fillRect(x + 1, y + 17, 42, 1, !inv);

  // LED indicator dot
  renderer.fillRect(x + 34, y + 19, 4, 3, !inv);
}

void HomeActivity::render(Activity::RenderLock&&) {
  const int W = renderer.getScreenWidth();
  const int H = renderer.getScreenHeight();

  renderer.clearScreen();

  // ==================== MENU BAR ====================
  constexpr int menuBarH = 26;
  renderer.drawLine(0, menuBarH, W - 1, menuBarH);
  renderer.drawText(UI_10_FONT_ID, 12, 6, "File", true);
  renderer.drawText(UI_10_FONT_ID, 58, 6, "Edit", true);
  renderer.drawText(UI_10_FONT_ID, 104, 6, "View", true);
  renderer.drawText(UI_10_FONT_ID, 156, 6, "Special", true);

  // Battery in menu bar (right side)
  const uint16_t battPct = battery.readPercentage();
  char battText[8];
  snprintf(battText, sizeof(battText), "%d%%", battPct);
  int battTextW = renderer.getTextWidth(UI_10_FONT_ID, battText);
  const int biX = W - battTextW - 34;
  const int biY = 12;
  renderer.drawRect(biX, biY, 18, 10);
  renderer.fillRect(biX + 18, biY + 3, 2, 4);
  int fillW = (14 * static_cast<int>(battPct)) / 100;
  if (fillW > 0) {
    renderer.fillRect(biX + 2, biY + 2, fillW, 6);
  }
  renderer.drawText(UI_10_FONT_ID, W - battTextW - 10, 6, battText, true);

  // ==================== FINDER WINDOW ====================
  constexpr int winX = 14, winY = 38;
  const int winW = W - 28;
  const int winH = H - 98;

  // Window double border
  renderer.drawRect(winX, winY, winW, winH);
  renderer.drawRect(winX + 1, winY + 1, winW - 2, winH - 2);

  // Drop shadow
  renderer.fillRect(winX + 3, winY + winH, winW - 1, 2);
  renderer.fillRect(winX + winW, winY + 3, 2, winH - 1);

  // ---- Title bar ----
  constexpr int tbH = 24;
  const int tbY = winY + 2;
  const int tbInnerX1 = winX + 2;
  const int tbInnerX2 = winX + winW - 3;

  // Title bar horizontal stripes
  for (int y = tbY + 2; y < tbY + tbH - 1; y += 2) {
    renderer.drawLine(tbInnerX1 + 20, y, tbInnerX2 - 18, y);
  }

  // Close box
  renderer.fillRect(tbInnerX1 + 4, tbY + 4, 14, 14, false);
  renderer.drawRect(tbInnerX1 + 4, tbY + 4, 14, 14);

  // Title text
  const char* winTitle = "Xteink HD";
  const int titleW = renderer.getTextWidth(UI_10_FONT_ID, winTitle, EpdFontFamily::BOLD);
  const int titleX = winX + (winW - titleW) / 2;
  renderer.fillRect(titleX - 8, tbY + 1, titleW + 16, tbH - 2, false);
  renderer.drawText(UI_10_FONT_ID, titleX, tbY + 4, winTitle, true, EpdFontFamily::BOLD);

  // ---- Info bar ----
  const int infoY = tbY + tbH;
  constexpr int infoH = 24;
  renderer.drawLine(winX + 2, infoY, tbInnerX2, infoY);

  const int itemCount = getMenuItemCount();
  char infoText[64];
  snprintf(infoText, sizeof(infoText), "%d items", itemCount);
  renderer.drawText(SMALL_FONT_ID, winX + 12, infoY + 5, infoText);

  renderer.drawLine(winX + 2, infoY + infoH, tbInnerX2, infoY + infoH);

  // ==================== ICON GRID ====================
  const int contentY = infoY + infoH + 1;
  const int contentW = winW - 4;
  const int contentX = winX + 2;

  // Build items list matching loop() order: recent books first, then menu items
  constexpr int ICON_FOLDER = 0;
  constexpr int ICON_COVER = 1;
  constexpr int ICON_HARDDRIVE = 2;
  constexpr int ICON_SETTINGS = 3;
  constexpr int ICON_TRANSFER = 4;
  constexpr int coverThumbH = 44;

  std::vector<std::string> gridLabels;
  std::vector<int> gridIcons;

  // Recent books (cover or document fallback)
  for (const auto& book : recentBooks) {
    gridLabels.push_back(book.title.empty() ? "Currently Reading" : book.title);
    gridIcons.push_back(ICON_COVER);
  }

  // Static menu items
  gridLabels.push_back(tr(STR_BROWSE_FILES));
  gridIcons.push_back(ICON_HARDDRIVE);
  gridLabels.push_back(tr(STR_MENU_RECENT_BOOKS));
  gridIcons.push_back(ICON_FOLDER);
  if (hasOpdsUrl) {
    gridLabels.push_back(tr(STR_OPDS_BROWSER));
    gridIcons.push_back(ICON_FOLDER);
  }
  gridLabels.push_back(tr(STR_FILE_TRANSFER));
  gridIcons.push_back(ICON_TRANSFER);
  gridLabels.push_back(tr(STR_SETTINGS_TITLE));
  gridIcons.push_back(ICON_SETTINGS);

  constexpr int cols = 3;
  const int cellW = contentW / cols;
  constexpr int cellH = 120;
  const int numItems = static_cast<int>(gridLabels.size());
  const int gridStartY = contentY + 20;

  for (int i = 0; i < numItems; i++) {
    const int col = i % cols;
    const int row = i / cols;
    const int cellX = contentX + col * cellW;
    const int cellY = gridStartY + row * cellH;
    const int cellCenterX = cellX + cellW / 2;
    const bool sel = (i == selectorIndex);

    // Draw icon based on type
    if (gridIcons[i] == ICON_COVER && i < static_cast<int>(recentBooks.size()) &&
        !recentBooks[i].coverBmpPath.empty()) {
      // Try to draw book cover thumbnail
      std::string coverPath = UITheme::getCoverThumbPath(recentBooks[i].coverBmpPath, coverThumbH);
      FsFile coverFile;
      bool coverDrawn = false;
      if (Storage.openFileForRead("HOME", coverPath, coverFile)) {
        Bitmap coverBmp(coverFile);
        if (coverBmp.parseHeaders() == BmpReaderError::Ok) {
          int bw = coverBmp.getWidth();
          int bh = coverBmp.getHeight();
          int bx = cellCenterX - bw / 2;
          int by = cellY + 2 + (48 - bh) / 2;
          if (sel) {
            renderer.drawRect(bx - 3, by - 3, bw + 6, bh + 6);
            renderer.drawRect(bx - 2, by - 2, bw + 4, bh + 4);
          }
          renderer.drawBitmap(coverBmp, bx, by, bw, bh, 0, 0);
          coverDrawn = true;
        }
      }
      if (!coverDrawn) {
        if (sel) {
          renderer.fillRect(cellCenterX - 28, cellY + 2, 56, 48);
        }
        drawMacDocumentIcon(cellCenterX, cellY + 24, sel);
      }
    } else {
      // Standard icon with selection highlight
      if (sel) {
        renderer.fillRect(cellCenterX - 28, cellY + 2, 56, 48);
      }
      switch (gridIcons[i]) {
        case ICON_HARDDRIVE:
          drawMacHardDriveIcon(cellCenterX, cellY + 24, sel);
          break;
        case ICON_SETTINGS:
          drawMacSettingsIcon(cellCenterX, cellY + 24, sel);
          break;
        case ICON_TRANSFER:
          drawMacTransferIcon(cellCenterX, cellY + 24, sel);
          break;
        default:
          drawMacFolderIcon(cellCenterX, cellY + 24, sel);
          break;
      }
    }

    // Label
    const std::string truncLabel = renderer.truncatedText(UI_10_FONT_ID, gridLabels[i].c_str(), cellW - 10);
    const int labelW = renderer.getTextWidth(UI_10_FONT_ID, truncLabel.c_str());
    const int labelX = cellCenterX - labelW / 2;
    const int labelY = cellY + 56;

    if (sel) {
      renderer.fillRect(labelX - 4, labelY - 3, labelW + 8, 28);
      renderer.drawText(UI_10_FONT_ID, labelX, labelY, truncLabel.c_str(), false);
    } else {
      renderer.drawText(UI_10_FONT_ID, labelX, labelY, truncLabel.c_str(), true);
    }
  }

  // ==================== BUTTON HINTS ====================
  const auto btnLabels = mappedInput.mapLabels("", tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, btnLabels.btn1, btnLabels.btn2, btnLabels.btn3, btnLabels.btn4);

  renderer.displayBuffer();

  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
  }
}
