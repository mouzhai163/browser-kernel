// Simprint compatibility shim.
//
// Upstream removed chrome/browser/ui/browser_list.h in favour of
// GlobalBrowserCollection. This keeps the range-for idiom
// `for (Browser* browser : *BrowserList::GetInstance())` working. Iteration is
// over a snapshot taken when the loop starts (creation order).

#ifndef CHROME_BROWSER_UI_BROWSER_LIST_H_
#define CHROME_BROWSER_UI_BROWSER_LIST_H_

#include <vector>

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"

class BrowserList {
 public:
  using const_iterator = std::vector<Browser*>::const_iterator;

  static BrowserList* GetInstance() {
    static BrowserList instance;
    return &instance;
  }

  // Refreshes the snapshot; the matching end() reads the same snapshot.
  const_iterator begin() {
    snapshot_.clear();
    GlobalBrowserCollection::GetInstance()->ForEach(
        [&](BrowserWindowInterface* browser_window) {
          snapshot_.push_back(static_cast<Browser*>(browser_window));
          return true;
        });
    return snapshot_.begin();
  }
  const_iterator end() { return snapshot_.end(); }

 private:
  BrowserList() = default;
  std::vector<Browser*> snapshot_;
};

#endif  // CHROME_BROWSER_UI_BROWSER_LIST_H_
