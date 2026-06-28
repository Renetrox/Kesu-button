# Kesú - XFCE panel plugin
PREFIX ?= /usr
PKG_CONFIG ?= pkg-config
PLUGIN_ID = kesu
TARGET = libkesu.so
SOURCES = kesu-panel-plugin.c
PLUGINDIR = $(PREFIX)/share/xfce4/panel/plugins
LIBDIR = $(shell $(PKG_CONFIG) --variable=libdir libxfce4panel-2.0)/xfce4/panel/plugins
CC ?= gcc
DEBUG ?= 0
PKGS = gtk+-3.0 libxfce4panel-2.0 libxfce4ui-2 libxml-2.0
CFLAGS += -Wall -Wextra -fPIC -std=c99
CFLAGS += $(shell $(PKG_CONFIG) --cflags $(PKGS))
LDFLAGS += -shared
LIBS += $(shell $(PKG_CONFIG) --libs $(PKGS))
ifeq ($(DEBUG),1)
	CFLAGS += -g -DDEBUG
else
	CFLAGS += -O2 -DNDEBUG
endif
all: $(TARGET)
$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIBS)
clean:
	rm -f $(TARGET)
install: all
	install -d $(DESTDIR)$(LIBDIR)
	install -d $(DESTDIR)$(PLUGINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(LIBDIR)/
	install -m 644 kesu.desktop $(DESTDIR)$(PLUGINDIR)/
uninstall:
	rm -f $(DESTDIR)$(LIBDIR)/$(TARGET)
	rm -f $(DESTDIR)$(PLUGINDIR)/kesu.desktop
.PHONY: all clean install uninstall
