import pyinotify

wm = pyinotify.WatchManager()

mask = pyinotify.IN_CREATE | pyinotify.IN_MODIFY

class EventHandler(pyinotify.ProcessEvent):
    def process_IN_CREATE(self, event):
        print "Creating: ", event.pathname
    def process_IN_MODIFY(self, event):
        print "Modifying: ", event.pathname


handler = EventHandler()
notifier = pyinotify.Notifier(wm, handler)

wdd = wm.add_watch('/home/hironobu/tmp', mask, rec=True)

notifier.loop()
