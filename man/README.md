* How to maintain the man pages of LTFS

Man pages for LTFS is originally written by SGML (DocBook V4.1). And it is converted to man (troff) by `docbook2man`.

At this time, we confirmed only `docbook2man` provided by `docbook-utils` on RHEL7 and RHEL8 can convet the docbook in the sgml directory. Thus we don't integrate auto build of man pages into build process. You can rebuild all man pages with `make man-rebuild` in this directory when you change the sgml documents (Please don't edit man page directly).

The folllowing link is useful to refer how to write `docbook::reference` document in SGML.

http://www.fifi.org/doc/docbook-doc/r43656.html
