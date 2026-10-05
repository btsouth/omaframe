FROM omaframe-ci:latest
RUN pacman -S --noconfirm --needed pulseaudio
