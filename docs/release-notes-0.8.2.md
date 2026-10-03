# Omaframe 0.8.2

Copying screenshot text now reads the current crop and edits. Redacted or
cropped-out content is excluded, and delayed OCR cannot overwrite a newer
screenshot clipboard copy.

If a screenshot draft cannot be saved, the image and edits stay open. Retry
the save or explicitly discard the unsaved edits before leaving.

Video drafts now restore trim, cuts, mute and camera layout together. Hidden
camera tracks remain hidden after reopening. Extending an annotation's time
range keeps it selected and supports undo or cancellation.

Cancelling the recording countdown stops the camera preview.

Validated with all thirteen regression suites in an isolated Omarchy desktop,
real OCR and clipboard checks, and an app smoke test that copied public text
without a redacted fake token. Physical GPU, microphone and camera combinations
were not retested for this release.

Update through `omarchy update` or `sudo pacman -Syu`, then reopen Omaframe.
Existing captures, settings and drafts are preserved.
