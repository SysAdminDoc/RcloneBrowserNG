#pragma once

#include <QLayout>
#include <QLayoutItem>
#include <QWidget>

// A control added at runtime through a failed layout downcast stays a bare
// child of its parent widget: it is constructed, findChildren() returns it,
// it has an accessible name and a sensible size hint, and it is in no layout
// at all. Existence checks and accessibility smoke tests walk straight past
// that, so layout participation has to be asserted directly.
namespace LayoutAssertions {

// QLayout::indexOf only sees direct items, so a widget placed in a nested
// layout (a button row inside a form field, say) needs a recursive search.
inline bool LayoutContains(const QLayout *layout, const QWidget *widget) {
  if (layout == nullptr) {
    return false;
  }
  for (int i = 0; i < layout->count(); ++i) {
    QLayoutItem *item = layout->itemAt(i);
    if (item->widget() == widget) {
      return true;
    }
    if (LayoutContains(item->layout(), widget)) {
      return true;
    }
  }
  return false;
}

// True when `widget` participates in the layout of its parent widget.
inline bool IsManagedByParentLayout(const QWidget *widget) {
  if (widget == nullptr || widget->parentWidget() == nullptr) {
    return false;
  }
  return LayoutContains(widget->parentWidget()->layout(), widget);
}

} // namespace LayoutAssertions
