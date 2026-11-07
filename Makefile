# Shortcuts for development. The real build is CMake; the widget scripts live in scripts/.
BUILD ?= build

.PHONY: all build reload preview watch watch-panel install release clean help

all: build

help:
	@echo "make build        build the picker into $(BUILD)/"
	@echo "make reload       install the widget and restart plasmashell"
	@echo "make preview      install the widget and open it in a standalone window"
	@echo "make watch        preview window, relaunched on every save in plasmoid/"
	@echo "make watch-panel  restart plasmashell on every save in plasmoid/"
	@echo "make install      install picker + widget to ~/.local (PREFIX=... to change)"
	@echo "make release      .plasmoid, source tarball, binary and checksums in $(BUILD)/"
	@echo "make clean        remove $(BUILD)/"

$(BUILD)/CMakeCache.txt:
	cmake -S . -B $(BUILD)

build: $(BUILD)/CMakeCache.txt
	cmake --build $(BUILD)

reload:
	scripts/reload-widget.sh

preview: build
	scripts/reload-widget.sh --window

watch: build
	scripts/watch-widget.sh

watch-panel: build
	scripts/watch-widget.sh --panel

PREFIX ?= $(HOME)/.local
install: build
	cmake --install $(BUILD) --prefix $(PREFIX)

release: build
	cmake --build $(BUILD) --target release

clean:
	rm -rf $(BUILD)
