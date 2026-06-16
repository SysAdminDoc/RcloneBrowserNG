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
            NSBitmapImageRep *rep = [[NSBitmapImageRep alloc] initWithCGImage:imageRef];
            NSData *data = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
            QPixmap pixmap;
            pixmap.loadFromData(QByteArray::fromRawData(
                static_cast<const char *>(data.bytes),
                static_cast<qsizetype>(data.length)), "PNG");
            icon = QIcon(pixmap);
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
