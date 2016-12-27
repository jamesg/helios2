Helios 2
========

A simple, private photograph album for the web browser.  Organise photographs
into albums and set tags and location data.

Features
--------

* Upload and store JPEG images.
* Sort images into albums.
* Attach a location and tags to images.

Motivation
----------

I take a lot of photographs and want a simple way to store and recall them
across several computers. These include a laptop, an Android tablet and a
Raspberry Pi running as a desktop computer.

Technology
----------

The server is written in C++ while the web interface uses Backbone.js.  SQLite
is used as the storage method for all data, including image data.

GNU libmicrohttpd is used as an embedded web server.  It runs in
thread-per-connection mode, which gives good performance on a quad-core
Raspberry Pi and adequate performance on single-core machines.

The entire web interface is compiled into the application binary using the
linker, which means the only file to be copied in order to distribute the
application is the binary.

Building and Running
--------------------

Libexiv2, libmagick++, libsqlite3 and libmicrohttpd must be present.  On Debian
and similar distributions, use:

    sudo apt-get install libexiv2-dev libmagick++-dev libsqlite3-dev \
    libmicrohttpd-dev

to install the required packages.  Then, simply run 'make' to compile
everything with Clang.  Other compilers are fine, but the Makefilue will need
to be modified.

The application is contained in the 'webserver' binary.  It can be invoked as:

    ./webserver -d database.db -p 8000

