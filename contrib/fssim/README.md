# LTFS File System Simulator (fssim)

The major change in version 2.5 of the [LTFS Format Specification](https://www.snia.org/sites/default/files/technical_work/LTFS/LTFS_Format_v2.5_Technical_Position.pdf) is the addition of Incremental Indexes.  Incremental Indexes allow an LTFS index written to the Data Partition of the tape to contain only the changes from the prior index, rather than the full state of the file system.  For a full discussion of Incremental Indexes, see Appendix H of the Format Specification referenced above.

When we first proposed Incremental Indexes there was considerable discussion concerning what they would need to contain, and how they would be implemented.  I decided to write a simulator that would mimic file system operations, and create full and incremental XML indexes very similar to those in LTFS as a Proof of Concept.  This simulator also includes programs to merge full and incremental directories created by it and verify the results, as well as a directory of test cases built by various members of the LTFS project.

The programs are meant to demonstrate how Incremental Indexes work, and also to potentially serve as an example of how they could be implemented.  

Full documentation of the programs is contained in the README file in the docs directory.

