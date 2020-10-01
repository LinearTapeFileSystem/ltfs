#
# File System Simulator for LTFS Common Routines
#
# This module is imported into fssim and fsidxupd, and provides common
# values, objects, and functions used by both programs.
#
# (c) David Pease - IBM Almaden Research Center - May 2018
# (c) David Pease - pease@coati.com - Sept. 2020
#

from   copy      import copy as pyobjcopy
from   datetime  import datetime
import fsglobals as g

useNextOID = None               # constant for readability
Log = {}                        # dictionary to hold update log entries

###########################################################################
# object flag bit values                                                  #
###########################################################################

modified = 0x01                 # object has been modified since last index
new      = 0x02                 # directory is newly created since last index

###########################################################################
# Directory object class                                                  #
###########################################################################

class Dir(object):
   def __init__(self, parent, oid=None, time=None):
      self.oid      = oid
      self.flags    = new
      if time:                # local code to avoid setting modified attribute
         self._modTime = time
      else:
         self._modTime = str(datetime.now())
      self.parent   = parent
      self.contents = {}
   @property
   def oid(self):
      return self._oid
   @oid.setter
   def oid(self, oid):
      if oid is not None:
         self._oid = oid
      else:
         self._oid = g.nextoid
         g.nextoid += 1
   @property
   def parent(self):
      return self._parent
   @parent.setter
   def parent(self, parent):
      self._parent = parent
   @property
   def modTime(self):
      return self._modTime
   @modTime.setter
   def modTime(self, time=None):
      if time:
         self._modTime = time
      else:
         self._modTime = str(datetime.now())
      self.isModified = True
   @property
   def isDir(self):
      return True
   @property
   def isFile(self):
      return False
   @property
   def isModified(self):
      return bool(self.flags & modified)
   @isModified.setter
   def isModified(self, ismodified):
      if ismodified:
         self.flags |= modified
      else:
         self.flags &= (0xff - modified)
   @property
   def isNew(self):
      return bool(self.flags & new)
   @isNew.setter
   def isNew(self, isnew):
      if isnew:
         self.flags |= new
      else:
         self.flags &= (0xff - new)
   @property
   def children(self):
      return self.contents.keys()
   def contains(self, name):
      return name in self.contents
   def obj(self, name):
      if not name in self.contents:
         return None
      return self.contents[name]
   def addObj(self, name, obj, update=True):
      self.contents[name] = obj
      if update:
         self.modTime = str(datetime.now())
         self.isModified = True
      return True
   def replObj(self, name, obj):
      if name not in self.contents:
         return False
      self.contents[name] = obj
      return True
   def rmObj(self, name, verifyEmpty=True, update=True):
      if not name in self.contents:
         return False
      obj = self.contents[name]
      if obj.isDir and verifyEmpty:
         if len(obj.contents) > 0:
            return False
      del self.contents[name]
      if update:
         self.modTime = str(datetime.now())
         self.isModified = True
      return True
   def __str__(self):
      flags = ["new," * self.isNew, "mod," * self.isModified]
      str = "Dir: oid %i, flags: '%s', parent:%i, size:%i %s %s" % \
          (self.oid, "".join(flags)[:-1], self.parent.oid, len(self.contents),
           self.modTime[5:16], repr(self).split()[-1][2:-1])
      return str

###########################################################################
# File object class                                                       #
###########################################################################

class File(object):
   def __init__(self, parent, oid=None, time=None):
      self.oid     = oid
      self.flags   = modified
      self.modTime = time
      self.parent  = parent
      self.data    = ""
   @property
   def oid(self):
      return self._oid
   @oid.setter
   def oid(self, oid):
      if oid is not None:
         self._oid = oid
      else:
         self._oid = g.nextoid
         g.nextoid += 1
   @property
   def parent(self):
      return self._parent
   @parent.setter
   def parent(self, parent):
      self._parent = parent
   @property
   def modTime(self):
      return self._modTime
   @modTime.setter
   def modTime(self, time=None):
      if time:
         self._modTime = time
      else:
         self._modTime = str(datetime.now())
      self.isModified = True
   @property
   def isDir(self):
      return False
   @property
   def isFile(self):
      return True
   @property
   def isModified(self):
      return bool(self.flags & modified)
   @isModified.setter
   def isModified(self, ismodified):
      if ismodified:
         self.flags |= modified
      else:
         self.flags &= (0xff - modified)
   @property
   def data(self):
      return self._data
   @data.setter
   def data(self, data):
      self._data = data if data else ""
   def __str__(self):
      flag = "mod" * self.isModified
      str = "File: oid %i, flags:'%s', parent:%i, size:%i %s %s" % \
             (self.oid, flag, self.parent.oid, len(self.data),
              self.modTime[5:16], repr(self).split()[-1][2:-1])
      return str

###########################################################################
#   python version-agnostic print function                                #
###########################################################################

def fsprt(*args):
   outstr = ""
   for arg in args:
      outstr += (str(arg) + " ")
   print(outstr)

###########################################################################
#   debugging-only print function                                         #
###########################################################################

def dbprt(*args):
   if g.debug:
      fsprt(*args)

###########################################################################
#   create a log entry                                                    #
###########################################################################

def log(name, oid, reason):
   logkey = name + "#" + str(oid)
   # don't overwrite a "New" log entry with a "Mod" one for the same object
   if logkey in Log and Log[logkey][0] == "New":
      if reason == "Mod":
         dbprt("Skip log: Mod for", name, "- is already New")
         return
   # trim unneeded log entries if parents are New or Deleted
   # (processing log entries in reverse time order - why does it matter??)
   for entkey in sorted(Log, key=lambda entkey: Log[entkey][1], reverse=True):
      entpath, entoid = entkey.rsplit("#", 1)
      entreason, enttime = Log[entkey]
      # if a parent dir is already "New", don't bother to log updates
      if entreason == "New" and name.startswith(joinPath(entpath,"/")):
         dbprt("Skip log", reason, "for", name, "- parent is New")
         return
      # if this is a parent being deleted, remove unneeded entries for children
      elif reason == "DelD":
         if entpath.startswith(joinPath(name, "/")):
            dbprt("Remove log", entreason, "for", entpath, "- parent Deleted")
            del Log[entkey]
   Log[logkey] = (reason, datetime.now())

###########################################################################
#   display log entries in pathname (processing) or time order            #
###########################################################################

def printLog(timeOrder=False):
   if timeOrder:
      sortkey = lambda k: Log[k][1]
   else:
      sortkey = None
   for k in sorted(Log, key=sortkey):
      if timeOrder:
         fsprt(str(Log[k][1])[:-7], Log[k][0], k)
      else:
         fsprt("%-20s" % k, Log[k][0], str(Log[k][1])[:-7])
   return True

###########################################################################
#   file system common utility functions                                  #
###########################################################################

# return a fully-qualified name (with no trailing slash)
def fullName(name):
   dbprt("fullName in:", name)
   if name.startswith("/"):
      fullname = name
   else:
      fullname = joinPath(g.curnm, name)
   if len(fullname) > 1 and fullname.endswith("/"):
      fullname = fullname[:-1]
   dbprt("fullName out:", fullname)
   return fullname 

# return full path and name portions of a filespec
def splitPath(name):
   path,name = fullName(name).rsplit("/",1)
   if not path:
      path = "/"
   return path,name

# return a properly joined path and file name
def joinPath(path, name):
   if name.startswith("/"):
      name = name[1:]
   if path.endswith("/"):
      return path + name
   else:
      return path + "/" + name

# process a directory path, output depends on parentRef value:               
#   False returns: full name of object, object instance   (default behavior)
#   True  returns: full path to object, parent object instance, object name
def dirpath(name, parentRef=False):
   fullname = fullName(name)
   if not parentRef and fullname != "/":
      fullname += "/"
   dbprt("dirpath in:", fullname)
   wdir = g.root
   wdnm = "/"
   parts = fullname[1:].split("/")
   if parts[-1]:
      lastpart = len(parts)-1
   else:
      lastpart = len(parts)-2
   for cnt, dn in enumerate(parts[:-1]):
      if wdir.contains(dn):
         if wdir.obj(dn).isDir or cnt == lastpart:
            wdir = wdir.obj(dn)
            wdnm = joinPath(wdnm, dn)
            continue
      dbprt("dirpath out: None")
      if parentRef: 
         return None, None, None
      else:
         return None, None
   dbprt("dirpath out:", wdnm, wdir.oid, parts[-1])
   if parentRef:
      return wdnm, wdir, parts[-1]
   else:
      return wdnm, wdir

# depth-first-search of a file system tree with function invocation on descent
# parameters:
#   sdobj   - starting directory object (top of dfs search tree)
#   sdnm    - the fully-qualified name of the starting directory
#   func    - the function to be invoked while traversing the tree
#   params  - a tuple of parameters to pass to func
# return value:
#   boolean  - from "enter" into a directory, flag indicating whether to
#              descend into (that is process the members of) that directory,
#              otherwise flag from func that halts processing of dir if False
# parameters passed to func:
#   reason  - one of "enter", "exit", "file", or "dir"
#   sdobj, sdnm, object name if "file" or "dir" else None, params  
def dfs(sdobj, sdnm, func, params):
   if sdnm == "/":
      name = "/"
   else: 
      name = splitPath(sdnm)[1]
   descend = func("enter", sdobj, sdnm, None, params)
   if descend:
      for name in sorted(sdobj.children,    # process files before subdirs
            key=lambda name: 'x\ff'+name if sdobj.obj(name).isDir else name):
         if sdobj.obj(name).isFile:
            if not func("file", sdobj, sdnm, name, params):
               return False
         else:
            if not func("dir", sdobj, sdnm, name, params):
               return False
            wdir = sdobj.obj(name)
            wdnm = joinPath(sdnm, name)
            if not dfs(wdir, wdnm, func, params):
               return False
      return func("leave", sdobj, sdnm, None, params)
   else:
      return True

# create a new directory or file obj, add it to parent, return object instance
def makeObj(name, dir=False, oid=None, ts=None, update=True):
   path, parent, newname = dirpath(name, parentRef=True)
   if parent == None:
      return None
   if parent.contains(newname):
      return None
   if dir:
      newobj = Dir(parent, oid=oid, time=ts)
      state = "New"
   else:
      newobj = File(parent, oid=oid, time=ts)
      state = "Mod"
   log(joinPath(path, newname), newobj.oid, state)
   parent.addObj(newname, newobj, update=update)
   return newobj

# remove a directory or file
def rmObj(name, dir=False, update=True):
   if name == "/":
      return False                                   # can't remove root
   path, parent, oldname = dirpath(name, parentRef=True)
   if parent == None or not parent.contains(oldname):
      return False
   obj = parent.obj(oldname)
   if dir:
      if obj.isFile:
         return False
      if obj == g.curdir:
         return False                                # can't remove current dir
   else:
      if obj.isDir:
         return False
   if not parent.rmObj(oldname, update=update): # for dir, parent will if empty
      return False
   deltype = "DelD" if dir else "DelF"
   log(joinPath(path, oldname), obj.oid, deltype)
   return True

# print info about an object, including memory address (for debugging only)
def printObj(obj, prefix="", children=False):
   fsprt(prefix+str(obj))
   if children and obj.isDir:
      for child in obj.children:
         fsprt("   "+str(obj.obj(child)))

# move or rename a directory or file, or copy a single file
def movecopy(srcname, tgtname, copy=False):
   # get full paths of source and target
   if srcname == "/":        # can't move root, copying it would be recursive
      return False
   if tgtname == ".":
      tgtname = g.curnm
   fulltgt = tgtname         # save original name for later
   # get source and target object and name information
   srcpath, srcobj = dirpath(srcname)
   tgtpath, tgtparent, tgtname = dirpath(tgtname, parentRef=True)
   if not srcobj or not tgtparent:
      return False
   # don't allow recursive copy
   if tgtpath.startswith(srcpath+"/"):
      return False
   # cannot copy a complete directory
   if copy and srcobj.isDir:
      return False
   dbprt("source:", srcpath, srcobj.oid)
   dbprt("target:", tgtpath, tgtparent.oid, tgtname)
   srcname = splitPath(srcpath)[1]
   # if target is simply "/", must have a file name to continue
   if not tgtname: 
      tgtname = srcname
   # if target is a directory, make it new parent and copy source filename
   if tgtparent.contains(tgtname) and tgtparent.obj(tgtname).isDir:
      tgtpath, tgtparent = dirpath(fulltgt)
      tgtname = srcname
      dbprt("new target info:", tgtpath, tgtname, tgtparent.oid)
   # now that we have all the object info, make sure not moving object to self
   srcparent = srcobj.parent
   if srcparent == tgtparent and srcname == tgtname: 
      dbprt("Move to self!")
      return False
   # if target already exists, decide what needs to be done (quite complex!)
   remtobj = None                       # flag to delete old target object
   if tgtparent.contains(tgtname):
      # if the target is a directory
      if tgtparent.obj(tgtname).isDir:
         # and the source is a file
         if srcobj.isFile:
            # if target contains new object name, it must be a file
            if tgtparent.obj(tgtname).contains(srcname):
               if tgtparent.obj(tgtname).obj(srcname).isDir:
                  return False
               #  it's a file, so we must remove it
               else:
                  remtobj = tgtparent.obj(tgtname).obj(srcname)
      # the target is a file
      else:
         # if the source is a directory, can't move it to a file
         if srcobj.isDir:
            return False 
         # source is a file
         if not copy:
            remtobj = tgtparent.obj(tgtname)
      dbprt("tgt exists:", ("remove "+str(remtobj.oid)) if remtobj else "")
   # we're finally ready to move something
   dbprt("mvcp:", srcpath,srcname,srcobj.oid, tgtpath,tgtname,tgtparent.oid)
   # if copy, create a copy of the source object
   if copy:
      newobj = pyobjcopy(srcobj)     # new python object, copy of srcobj
      newobj.oid = useNextOID
   else:   # move  (move original object, make copy in source for deletion)
      newobj = srcobj                # newobj is srcobj (same python object)
      srcobj = pyobjcopy(newobj)     # create new python object copy for srcobj
      if not srcparent.replObj(srcname, srcobj): # replace srcobj ref in parent
         return False
   # set target object's parent and state
   newobj.parent = tgtparent
   if newobj.isDir:
      newobj.isNew = True
      status = "New"
   else:
      newobj.isModified = True
      status = "Mod"
   # if necessary, remove existing file that is being replaced
   if remtobj:
      remoid = tgtparent.obj(tgtname).oid
      if not tgtparent.rmObj(tgtname):  
         return False
      log(joinPath(tgtpath, tgtname), remoid, "DelF")
   # put moved/copied object in its new home
   tgtparent.addObj(tgtname, newobj)           # put object under target name
   log(joinPath(tgtpath, tgtname), newobj.oid, status)
   # for move, remove object from its old home
   if not copy:
      if not srcparent.rmObj(srcname, verifyEmpty=False): # rem src from parent
         fsprt("Error: cannot remove source object for move; object copied.")
         return False
      deltype = "DelF" if srcobj.isFile else "DelD"
      dbprt("Creating log:", deltype, srcpath, srcobj.oid)
      log(srcpath, srcobj.oid, deltype)
   if g.debug: 
      printObj(newobj, "mvcp new: ")
      printObj(srcobj, "mvcp src: ")
   return True

# copy a directory's contents to an existing directory recursively
def copyrecurs(inloc, outloc):
   # get source and target object and name information
   if outloc == ".":
      outloc = g.curnm
   srcpath, srcobj = dirpath(inloc)
   tgtpath, tgtobj = dirpath(outloc)
   if not srcobj or not tgtobj:
      return False
   # don't allow recursive copy
   if tgtpath.startswith(srcpath+"/"):
      return False
   # both source and target must be existing directories
   if not srcobj.isDir or not tgtobj.isDir:
      return False
   # recursively copy files and directories
   return dfs(srcobj, srcpath, copyObj, (srcpath, tgtpath)) 

# make a copy of a single file or directory (invoked through dfs())
def copyObj(desc, dirobj, path, name, moreparams):
   srcfname, tgtfname = moreparams
   if desc not in ("file", "dir"):
      return True
   dbprt("copyObj:", desc, dirobj.oid, path, name, srcfname, tgtfname)
   addpath = path[len(srcfname):]
   if addpath:
      tgtdir = tgtfname + addpath
   else:
      tgtdir = tgtfname 
   newname = joinPath(tgtdir, name)
   if  desc == "file":
      oldname = joinPath(path, name)
      dbprt("Copying file", oldname, "to", newname)
      return movecopy(oldname, newname, copy=True)
   elif desc == "dir":
      dbprt("Creating directory", newname)
      if not makeObj(newname, dir=True):
         return False
      # should copy other attributes from source to new dir here, if needed
   return True

###########################################################################
#   routines used in generating indexes                                   #
###########################################################################

# write a full index to a file (uses dfs to descend entire tree)
def fullIndex(fn):
   global indexLevel, Log
   fullfn = fn + "-full.xml"
   with open(fullfn, "w") as fh:
      fh.write("<fullindex>\n")
      indexLevel = 0                     # used only for indentation of xml
      indexEnt("dir", g.root, "/", None, ("full", fh))
      result = dfs(g.root, "/", indexEnt, ("full", fh))
      fh.write("</fullindex>\n")
      Log = {}                           # clear log after writing an index 
   if not result:
      fsprt("Error occurred creating full index")   
   return result

# write an incremental index to a file
def incrIndex(fn):
   global indexLevel, Log
   success = True
   incrfn = fn + "-incr.xml"
   with open(incrfn, "w") as fh:
      fh.write("<incrementalindex>\n")
      indexLevel = 0                 # used for indentation and to close paths
      # always start with an entry for the root dir, whether or not it's in log
      indexEnt("dir", g.root, "/", "", ("incr", fh))
      indexLevel += 1
      lastpath = "/"
      pathindir = None
      # process all log entries sorted in name,oid order
      for logKey in sorted(Log):
         # get full name, oid, log entry type and timestamp for this log entry
         fullname, oid = logKey.rsplit("#", 1)
         if fullname == "/" and oid == "0":    # we already did the root dir
            continue
         path, name = splitPath(fullname)
         reason, ts = Log[logKey]
         # if debugging, show log entry
         dbprt("Log entry: %s (%i) %s %s\n" % (fullname, int(oid), reason, ts))
         # get object reference for log target
         obj = dirpath(fullname)[1]       # already have full name, need obj 
         # didn't find object for this entry, error unless it was for deletion
         if not obj and not reason.startswith("Del"):
            dbprt("Object matching log entry not found", fullname, oid)
            success = False
            break
         # found the same name but a different oid, also bad unless delete
         if not reason.startswith("Del") and obj.oid != int(oid):
            dbprt("Object oids do not match:", fullname, oid, obj.oid)
            continue
         # close out unneeded directories from prior object
         lastdirs = lastpath.split("/")[1:] if lastpath != "/" else []
         newdirs = path.split("/")[1:] if path != "/" else []
         newLevel = len(newdirs)
         lastmatch = -1
         for i in range(min(len(lastdirs), len(newdirs))+1):
            if len(lastdirs) > i and len(newdirs) > i:
               if lastdirs[i] == newdirs[i]:
                  continue
            lastmatch = i-1          
            break
         i = 0                          # inialize in case following loops null
         for i in range(len(lastdirs)-1, lastmatch, -1):
            fh.write("      " * (i+1) + "   </contents>\n")
            fh.write("      " * (i+1) + "</directory>\n")
         # build new directory path structure as needed for object
         for i in range(lastmatch+1, newLevel):
            prefix = "      " * (i+1)
            temppath = "/".join(['']+newdirs[:i+1]) # full path name of dir
            if temppath == pathindir:               # if we just added this dir
               continue                             #   to the index, skip it
            fh.write(prefix + "<directory>\n")
            fh.write(prefix + "   <name>" + newdirs[i] + "</name>\n")
            tempobj = dirpath(temppath)[1]
            if tempobj.isModified:        # parent dir may have updated modtime
               fh.write(prefix + "   <time>"+str(tempobj.modTime)+"</time>\n")
               tempobj.isModified = False 
            fh.write(prefix + "   <contents>\n")
         indexLevel = newLevel + 1
         pathindir = None
         # process Deletes here rather than using indexEnt()
         if reason.startswith("Del"):
            lastpath = path   # make sure to save last path for deletes as well
            if obj:
               if obj.oid == int(oid):
                  dbprt("Found deleted object in file system", fullname, oid)
                  success = False
                  break
               else:
                  continue      # don't delete object if it's been recreated
            prefix = "      " * indexLevel
            indextype = "file" if reason == "DelF" else "directory"
            fh.write(prefix + "<" + indextype + ">\n")
            fh.write(prefix + "   <name>" + name + "</name>\n")
            fh.write(prefix + "   <deleted/>\n") 
            fh.write(prefix + "</" + indextype + ">\n")
            continue
         # write the object entry into the index
         newdir = obj.isDir and obj.isNew      # indexEnt() will reset this
         indextype = "file" if obj.isFile else "dir"
         indexdirobj = obj.parent if obj.parent else g.root
         indexEnt(indextype, indexdirobj, fullname, name, ("incr", fh))
         # if object was a New directory, output its full subtree
         if obj.isDir:  
            if newdir:
               if not dfs(obj, fullname, indexEnt, ("full", fh)):
                  dbprt("Error occurred in dfs for incremental index")
                  success = False
                  break
            else:
               pathindir = joinPath(path, name)  # this feels like a kludge!
         # save index state from this entry to compare to the next
         lastpath = path 
      # close out the directory tree and the incremental index
      for i in range(indexLevel-1, 0, -1):
         fh.write("      " * i + "   </contents>\n")
         fh.write("      " * i + "</directory>\n")
      fh.write("   </contents>\n")
      fh.write("</incrementalindex>\n")
   # clear the log and return
   Log = {}
   if not success:
      # delete incremental index file, but for now keep it for debugging
      fsprt("Error occurred creating incremental index")
   return success

# write the XML for single index entry (this function my be invoked directly,
# but is often invoked through dfs(), thus the use of params)
def indexEnt(desc, dirobj, path, name, params):
   global indexLevel
   enttype, fh = params
   dbprt("Index entry:", desc, dirobj.oid, path, name, enttype)
   if not name:                  # entering or leaving a directory, or root dir
      obj = dirobj               # obj is the directory itself
      if path != "/":
         name = splitPath(path)[1]
   else:
      obj = dirobj.obj(name)              # obj is a member of the directory
   if desc == "enter":
      indexLevel += 1
   elif desc == "file":
      prefix = "      " * indexLevel
      fh.write(prefix + "<file>\n")
      fh.write(prefix + "   <name>" + name + "</name>\n")
      fh.write(prefix + "   <oid>" + str(obj.oid) + "</oid>\n")
      # note: if enttype == "incr", should only output changed attributes
      fh.write(prefix + "   <time>" + str(obj.modTime) + "</time>\n")
      if obj.data or enttype == "incr":      # if incr, replace any old data
         fh.write(prefix + "   <data>" + obj.data + "</data>\n")
      fh.write(prefix + "</file>\n")
      obj.isModified = False 
   elif desc == "dir":
      prefix = "      " * indexLevel
      if obj != g.root:
         fh.write(prefix + "<directory>\n")
         fh.write(prefix + "   <name>" + name + "</name>\n")
      if enttype == "full" or obj.isNew:        # ??? does this match spec?
         fh.write(prefix + "   <oid>" + str(obj.oid) + "</oid>\n")
      fh.write(prefix + "   <time>" + str(obj.modTime) + "</time>\n")
      fh.write(prefix + "   <contents>\n")
      obj.isModified = False  
      obj.isNew = False  
   elif desc == "leave":
      indexLevel -= 1
      prefix = "      " * indexLevel
      if enttype == "full" or obj != g.root:
         fh.write(prefix + "   </contents>\n")
      if obj != g.root:
         fh.write(prefix + "</directory>\n")
   return True

