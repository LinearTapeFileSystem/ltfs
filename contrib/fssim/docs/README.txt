
LTFS File System Simulator (fssim) Usage Guide and Examples (README)

Overview
========

The File System Simulator programs are used to simulate file system activity, 
create full and incrmental indexes reflecting that activity, update a full 
index using incremental indexes, and verify the correctness of that update.

The first program in the set is fssim.  It simulates file system activity such 
as creating or deleting files and directories, moving and copying files or 
directories, listing information, etc.  It also writes full and incremental 
XML indexes to text files on request. 

The second program is fsidxupd (perhaps a poor choice of names).  It updates
a full index created by fssim with one or more incremental indexes created
sequentially in the same execution of fssim, then creates a full index
from the result.  The resulting full index should exactly match the 
corresponding full index from fssim.

The programs were written by David Pease at IBM's Almaden Research Center in May-August of 2018, and significantly revised and improved by the original author (now retired) in September of 2020.

Invoking the programs:
======================

The programs are python executables, meaning that they can be invoked from
the command line in Unix-like environments (such as Linux and Mac) as long as
the executable bit is on and /usr/bin/python points to the python interpreter.  
The programs are written so that they run correctly in either python 2 or
python 3.

In Windows, it should be possible to invoke them by a command such as "python 
fsXXX" or by renaming the two program files to fsXXX.py and invoking them by
name (if python is properly configured). 

There are two additional files besides the fssim and fsidxupd programs.  They
are fscommon.py and fsglobals.py.  Both of these are imported by the two
command programs; all four files should reside in the same directory.

The format of the fssim command is:

  fssim [-d] [command file]

where:
  -d:            prints a ton of debugging messages (probably meaningfull 
                 only to me) during processing
  command file:  executes commands from the specified text file at startup

    A command file is simply a text file containing commands as they would be 
    entered from the keyboard; blank lines and comment lines starting with "#" 
    are allowed.  Command files make it easy to design and debug a scenario, 
    then execute it multiple times (often while modifying code in-between).  
    After a command file is executed (without encountering an "exit" command), 
    the program drops into its normal keyboard input mode (which is terminated
    by entering "exit").

The format of the fsidxupd command is:

  fsidxupd [-d] <fullindex> [<incrindexes> ...]

where:
  -d:            prints lots of debugging messages during processing
  fullindex:     a file containing a full index from fssim
  incrindexes:   a list of files containing incremental indexes from fssim

The first file name must identify a full index created by fssim.  The second 
and subsequent names, if present, must identify incremental indexes created
sequentially by fssim.  The output of the program (currently) has a hard-coded 
file name of upd-full.xml

fssim Commands
==============

The fssim commands are closely modeled after the Unix shell commands that 
perform the same function; however, they are often greatly simplified versions
(the goal was to implement a simulator, not re-write bash).  

Unrecognized commands or commands with an invalid number of operands will
return a "** Invalid" message.  Commands that do not execute successfully
(for any reason) will return the message "** Failed".  That is the extent of
error messages.  (This is only a test program.)

The command prompt indicates the current working directory (as it typically
does in bash), and starts at root ("/").  File and directory names can be 
entered as full paths or relative to the current directory; however, the only 
place that ".." is recognized is as the single operand of a "cd" command, and
the only place that "." is recognized is as the single target for a mv or cp 
command. The directory separator character is "/".  The command prompt 
environment supports command recall and editing (has been tested on Linux and 
Mac).

The command processor respects strings enclosed in double quotes (").  
Quoted strings can be used to create object names with embedded blanks or 
to echo strings with arbitrary blank spaces.  For example:

echo Hello > "file 1"
echo " world!" >> "file 1"
cat "file 1"

The "help" command will print a short description of all of the commands. 
Here is a slightly more detailed command summary (square brackets indicate 
optional parameters, curly braces indicate a choice):

cat   name        - write a file's contents to the screen
cd    [dirname]   - change current directory; with no operands, changes to root
cls               - clear the screen (tested in Linux terminal window)
cp    [-r] name name - copy a file from one name to another; with -r option,
                    copy directory contents recursively
dir   [-a] [name] - an alias for ls
echo  [data] [{>|>>} name] - write data to the screen or to a file
fsck              - verify that all object flags have been correctly reset 
index [{-i|-f}] fileprefix - write full and/or incremental indexes to files 
                    using names prefixed with the specified prefix (see below)
ll    [name]      - list long: an alias for ls -a 
log   [-t]        - list current update log entries (optionally in time order)
ls    [-a] [name] - list file or directory info (see more info below)
md    dirname [...] - an alias for mkdir
mkdir dirname [...] - create directories or full directory paths
mv    name name   - move/rename a file or directory
rd    dirname     - an alias for rmdir
rm    filename    - remove a file
rmdir dirname     - remove a directory (if it is empty)
touch name [...]  - update the timestamp of existing files or directories,
                    or create new files (with directory paths if needed)
tree  [-o]        - show a tree view of the entire file system; with -o
                    option, also include oids

The ls (dir) command prefixes each file or directory with some attributes in
the order "dmn".  A "d" in the first position indicates a directory.  An "m"
in the second position indicates that the file or directory has been modified
since the last index was written.  An "n" in the third position indicates that
the directory is new since the last index (and thus must be written in its    
entirety with all of its children to an incremental index).  Following the 
attributes are the object id, the modification time, and the object size.  For 
files, the size is the number of data bytes in the file; for directories, it 
is the number of children in the directory.  When the object of the "ls" 
command is a directory, the "-a" (all) option will also show an entry (".") 
which lists the status of the directory itself.  (The ll command is shorthand 
for "ls -a".)

The tree command shows a full file system tree view; using the "-o" option 
shows the object ids (oids) following the object names, as in this example:

   / (0)
    |-file1 (1)
    |=dir1 (2)
      |=dir2 (4)
        |-file2 (5)
    |=dir3 (3)

As can be seen from the example, a "-" in front of a name in the tree view
indicates a file, while a "=" indicates a directory.  Within a directory
files are listed first followed by subdirectories, in alphabetical order.

The mkdir (md), touch, and echo commands will create any missing lower-level 
directories required in the directory or file path.

A note using on the cp command with the "-r" option (recursive copy).  This 
command option requires two existing directory names as input; it copies the 
*contents* of the first directory to the second, recursively.  Thus, using a 
directory structure such as that shown in the output of "tree" above, the 
following command: 

   cp -r dir1/dir2 dir3 

would copy only file2 to dir3.  In order to end up with dir2 and file2 under
dir3, one could use either

   cp -r dir1 dir3
 
or
   md dir3/dir2
   cp -r dir1/dir2 dir3/dir2

Note, however, that the first option will copy everything under dir1 to dir3;
if dir1 had files or directories other than dir2 under it, it would copy
everything and would not give the desired results.

Indexes
=======

By default, the index command writes both a full index and an increment index 
to a pair of files whose names start with the specified prefix.  For example, 
the command "index t0" will create files "t0-full.xml" and "t0-incr.xml" in 
the user's current directory.  (The rationale for writing both indices is that
that one can start from any full index written, apply any number of subsequent 
incremental indexes they wish, and the result should be the same as the full 
index corresponding to the last incremental index used.)  The index command
allow overriding the default behavior by using the "-i" (incremental index
only) or "-f" (full index only) options.  To simulate normal system operation,
(and minimize the number of files produced during testing), one could specify
"-f" for the index produced at the start of the run (e.g., "index -f t0"), then
use "-i" for all subsequent indexes up to the last one, and use neither flag 
for the final index.  This would give a starting and ending full index, and a 
full set of incremental indexes to validate against the final full index.

When testing indexes it's probably a good idea to start a command file with 
the command "index [-f] t0" (or the prefix of your choice); this creates a 
starting point for incremental indexes from the beginning of the session.  
index commands can be interspersed in a command stream at any point, and the 
resulting full and incremental indexes can be inspected (and processed  
using fsidxupd).

The fsck command searches the entire file system for any files or directories
with New or Modified flags.  This can be used after an index command with the
"-i" (incremental only) flag to verify that all objects with these flags have
been visited, and that all such flags have been correctly reset.

A pair of "t0" indexes (created at startup) looks like this:

   <fullindex>
      <oid>0</oid>
      <time>2018-05-17 19:09:54.827475</time>
      <contents>
      </contents>
   </fullindex>
   
   <incrementalindex>
      <oid>0</oid>
      <time>2018-05-17 19:09:54.827475</time>
      <contents>
      </contents>
   </incrementalindex>
   
The two are essentially identical, since the root directory is the only one in
existence, and it is new and thus shows up in the incremental index as well.  
(Actually, the t0 incremental index is useless, but it helps illustrate how 
indexes work.)

Invoking fssim with this set of commands (in a command file):
 
   index t0
   mkdir Dir1
   touch Dir2/Dir3/foo
   touch Dir2/Dir3/bar
   index t1
   touch Dir2/Dir3/baz
   rmdir Dir1
   index t2
   
produced the following two "t2" indexes:

   <fullindex>
      <oid>0</oid>
      <time>2018-05-17 19:14:37.004944</time>
      <contents>
         <directory>
            <name>Dir2</name>
            <oid>2</oid>
            <time>2018-05-17 19:14:37.004460</time>
            <contents>
               <directory>
                  <name>Dir3</name>
                  <oid>3</oid>
                  <time>2018-05-17 19:14:37.004918</time>
                  <contents>
                     <file>
                        <oid>5</oid>
                        <name>bar</name>
                        <time>2018-05-17 19:14:37.004521</time>
                     </file>
                     <file>
                        <oid>6</oid>
                        <name>baz</name>
                        <time>2018-05-17 19:14:37.004908</time>
                     </file>
                     <file>
                        <oid>4</oid>
                        <name>foo</name>
                        <time>2018-05-17 19:14:37.004491</time>
                     </file>
                  </contents>
               </directory>
            </contents>
         </directory>
      </contents>
   </fullindex>

   <incrementalindex>
      <time>2018-05-17 19:14:37.004944</time>
      <contents>
         <directory>
            <name>Dir1</name>
            <deleted/>
         </directory>
         <directory>
            <name>Dir2</name>
            <contents>
               <directory>
                  <name>Dir3</name>
                  <time>2018-05-17 19:14:37.004918</time>
                  <contents>
                     <file>
                        <name>baz</name>
                        <oid>6</oid>
                        <time>2018-05-17 19:14:37.004908</time>
                     </file>
                  </contents>
               </directory>
            </contents>
         </directory>
      </contents>
   </incrementalindex>

Using fsidxupd
==============

fsidxupd is used to verify the correctness of incremental indexes and their
application to a full index.  For example, using the sample list of commands
in the Index section above, three sets of indexes will be created by fssim
when running the commands; the file names will be t0-full.xml, t0-incr.xml, 
t1-full.xml, t1-incr.xml, t2-full.xml, and t2-incr.xml.  The following 
fsidxupd command will take the starting full index and apply the two appro-
priate incremental indexes:

   fsidxupd t0-full.xml t1-incr.xml t2-incr.xml
  
The output of this command will be a file named upd-full.xml.  In this example,
if the system is working correctly upd-full.xml will be identical to 
t2-full.xml.  In Unix-like systems, the command:
 
   diff upd-full.xml t2-full.xml

would show no output (no differences).

Program Organization
====================

The file fscommon.py contains all of the file system logic.  It contains 
object definitions for directory and file objects, and implements all of 
the low-level logic for managing those objects.  (In function, it roughly
corresponds to the vfs module in a Unix-like OS.)  It also contains the 
the python version-agnostic print function (fsprt).

The file fsglobals.py defines global variables that must be shared between
the different modules.  In order to provide a consistent naming reference
to these shared global variables, they must be in a separate python module
and that module must be imported with the same name in each program.  (For
succinctness, I import it as "g", so that global values are referenced as 
g.variablename.) 

The file fssim is the command processor for user file system commands; it
processes commands from a command file and/or the command line.  (In Unix
terms, it implements the user shell and shell commands.)

The file fsidxupd is a utility program that reads, updates, and writes 
indexes.  It uses file system functions from fscommon.py to update a full 
index using data from incremental indexes.

fsruntest
=========

The programs also include a bash shell script to automate testing.  The
format of the command is:

   fsruntest [-d] <command file>

The command file is used to run the test.  It may end with an "exit" command,
or the exit command can be entered manually while the test is running.  The
"-d" option runs fssim and fsidxupd with debugging messages enabled.

The script first invokes fssim, passing in the name of the command file.  It 
then invokes fsidxupd to apply all of the subsequent incremental indexes to 
the first full index file created.  Finally, it compares the output of fsidxupd
to the final full index created by fssim.  If the output of diff is not empty, 
the output is preceded by a highlighted error line (so that it stands out or 
can be searched for in a large test run).

Since it is a bash shell script, the test script runs on both Linux and Mac
systems, and with the proper Linux subsystem configured perhaps even on 
Windows 10 (or earlier, using Cygwin).

 
Final Comments
==============

The oids in the files and directories correspond to the fileuid in LTFS.
Initially I did not believe that oids (and thus fileuids) were important
to processing incremental indexes.  It turns out that I was wrong, and that
a unique identifier for an object is important to the correct application
of an incremental index in one important case.  Timestamps are the last 
object modification time.  I think all other object fields are obvious.

The programs have not been intensively debugged.  There may be lingering 
errors in the commands and/or in index generation.  It all seems to work,
but if you find errors (or even think you *may* have found an error) please 
let me know.  The better debugged the programs are, the more reliable our 
results will be.  

          -David Pease  -  pease@coati.com
