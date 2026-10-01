# Omaframe 0.7.1

Scrolling captures now keep the full selected width, preserving Chromium's
window controls and other content at the right edge. Automatic scrollbar
cropping previously removed those columns from headers and footers too.
Scrollbars remain in the result; use the crop tool if you want to remove them.

Regression checks compare complete headers and footers with the original,
including when the progress control overlaps the capture.

Install or update with the attached `omaframe-x86_64.pkg.tar.zst` package,
then reopen Omaframe. Existing settings, captures and drafts are preserved.
