// Simprint compatibility shim.
//
// Upstream removed chrome/browser/ui/browser_finder.h in favour of
// BrowserCollection (GlobalBrowserCollection / ProfileBrowserCollection).
// Simprint code and patches still use the old chrome::Find*() helpers, so they
// are re-implemented here on top of the new API. On desktop every
// BrowserWindowInterface is a Browser.

#ifndef CHROME_BROWSER_UI_BROWSER_FINDER_H_
#define CHROME_BROWSER_UI_BROWSER_FINDER_H_

#include <vector>

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"

namespace chrome {

// The most recently active browser, or nullptr.
inline Browser* FindLastActive() {
  return static_cast<Browser*>(
      GlobalBrowserCollection::GetInstance()->GetLastActiveBrowser());
}

// The most recently activated tabbed browser for `profile`, or nullptr.
inline Browser* FindTabbedBrowser(Profile* profile,
                                  bool match_original_profiles) {
  ProfileBrowserCollection* collection =
      ProfileBrowserCollection::GetForProfile(profile);
  if (!collection) {
    return nullptr;
  }
  return static_cast<Browser*>(
      collection->FindTabbedBrowser(match_original_profiles));
}

// All tabbed browsers whose profile is exactly `profile`.
inline std::vector<Browser*> FindAllTabbedBrowsersWithProfile(
    Profile* profile) {
  std::vector<Browser*> result;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* browser_window) {
        Browser* browser = static_cast<Browser*>(browser_window);
        if (browser->GetProfile() == profile &&
            browser->GetType() == BrowserWindowInterface::TYPE_NORMAL) {
          result.push_back(browser);
        }
        return true;
      });
  return result;
}

}  // namespace chrome

#endif  // CHROME_BROWSER_UI_BROWSER_FINDER_H_
