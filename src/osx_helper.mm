#include "osx_helper.h"
#include <Cocoa/Cocoa.h>
#include <ApplicationServices/ApplicationServices.h>

QIcon osxGetIcon(const QString& extension)
{
    QIcon icon;
    @autoreleasepool
    {
        NSImage* image = [[NSWorkspace sharedWorkspace] iconForFileType:extension.toNSString()];
        NSRect rect = NSMakeRect(0, 0, image.size.width, image.size.height);
        CGImageRef imageRef = [image CGImageForProposedRect:&rect context:NULL hints:nil];
        if (imageRef)
        {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            NSBitmapImageRep *rep = [[NSBitmapImageRep alloc] initWithCGImage:imageRef];
            NSData *data = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
            QPixmap pixmap;
            pixmap.loadFromData(QByteArray::fromRawData(
                static_cast<const char *>(data.bytes),
                static_cast<qsizetype>(data.length)), "PNG");
            icon = QIcon(pixmap);
#else
            icon = QtMac::fromCGImageRef(imageRef);
#endif
        }
    }
    return icon;
}

void osxShowDockIcon()
{
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
}

void osxHideDockIcon()
{
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
}
