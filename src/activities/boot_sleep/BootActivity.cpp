#include "BootActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "fontIds.h"

void BootActivity::onEnter() {
  Activity::onEnter();

  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  renderer.drawCenteredText(BOOKERLY_18_FONT_ID, pageHeight / 2 - 20, "\\[T]/", true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(BOOKERLY_18_FONT_ID, pageHeight / 2 + 30, "Hello, my king", true, EpdFontFamily::BOLD);
  renderer.displayBuffer();
}
