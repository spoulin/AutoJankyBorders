#import <AppKit/AppKit.h>
#include <bootstrap.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <libproc.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <signal.h>
#include <spawn.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../mach.h"
#include "options.h"

#define DAEMON_NAME "borders"
#define LOGIN_ITEM_LABEL "com.github.spoulin.bordersbar"
#define STATE_TIMEOUT_US 5000000
#define STATE_POLL_US 100000

extern char** environ;

enum daemon_action { DAEMON_START, DAEMON_STOP, DAEMON_RESTART };

// `brew services` names the job differently depending on the Homebrew version.
static const char* const g_service_labels[] = { "sh.brew.autojankyborders",
                                                "homebrew.mxcl.autojankyborders" };

static NSString* home_path(NSString* relative_path) {
  const char* home = getenv("HOME");
  NSString* root = home ? @(home) : NSHomeDirectory();
  return [root stringByAppendingPathComponent:relative_path];
}

static NSString* executable_path(void) {
  char path[PATH_MAX];
  uint32_t size = sizeof(path);
  char resolved[PATH_MAX];
  char stable[PATH_MAX];
  if (_NSGetExecutablePath(path, &size) != 0
      || !realpath(path, resolved)
      || !menubar_stable_path(resolved, stable, sizeof(stable))) return nil;
  return @(stable);
}

// Looks the service up directly: running `borders` with arguments when no daemon
// exists would turn that child process into the daemon.
static mach_port_t daemon_port(void) {
  mach_port_t port = 0;
  if (bootstrap_look_up(bootstrap_port, BS_NAME, &port) != KERN_SUCCESS) return 0;
  return port;
}

static bool daemon_running(void) {
  mach_port_t port = daemon_port();
  if (!port) return false;
  mach_port_deallocate(mach_task_self(), port);
  return true;
}

static void daemon_send(const struct menubar_options* options) {
  char message[MENUBAR_MESSAGE_SIZE];
  size_t length = menubar_options_message(options, message);
  mach_port_t port = daemon_port();
  if (!port) return;
  if (length) mach_send_message(port, message, (uint32_t)length);
  mach_port_deallocate(mach_task_self(), port);
}

static bool wait_for_daemon_state(bool running) {
  for (int waited = 0; waited < STATE_TIMEOUT_US; waited += STATE_POLL_US) {
    if (daemon_running() == running) return true;
    usleep(STATE_POLL_US);
  }
  return daemon_running() == running;
}

static int run_tool(char* const arguments[]) {
  pid_t pid;
  if (posix_spawn(&pid, arguments[0], NULL, NULL, arguments, environ) != 0) return -1;
  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR) return -1;
  }
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static const char* service_label(void) {
  for (size_t i = 0; i < sizeof(g_service_labels) / sizeof(*g_service_labels); ++i) {
    NSString* name = [NSString stringWithFormat:@"Library/LaunchAgents/%s.plist",
                                                g_service_labels[i]];
    if ([NSFileManager.defaultManager fileExistsAtPath:home_path(name)]) {
      return g_service_labels[i];
    }
  }
  return NULL;
}

static NSString* service_target(const char* label) {
  return [NSString stringWithFormat:@"gui/%u/%s", getuid(), label];
}

static int launchctl(const char* command, const char* option, NSString* target) {
  char* arguments[5] = { "/bin/launchctl", (char*)command };
  int count = 2;
  if (option) arguments[count++] = (char*)option;
  arguments[count++] = (char*)target.UTF8String;
  return run_tool(arguments);
}

static bool service_loaded(const char* label) {
  char* arguments[] = { "/bin/launchctl", "print",
                        (char*)service_target(label).UTF8String, NULL };
  pid_t pid;
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
  posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
  int result = posix_spawn(&pid, arguments[0], &actions, NULL, arguments, environ);
  posix_spawn_file_actions_destroy(&actions);
  if (result != 0) return false;
  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR) return false;
  }
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

// Without a disclaim, macOS attributes the daemon's Screen Recording use to this
// app instead of to the `borders` binary that the user granted permission to.
static void disclaim_responsibility(posix_spawnattr_t* attributes) {
  int (*set_disclaim)(posix_spawnattr_t*, int)
    = dlsym(RTLD_DEFAULT, "responsibility_spawnattrs_setdisclaim");
  if (set_disclaim) set_disclaim(attributes, 1);
}

static bool spawn_daemon(void) {
  NSString* directory = executable_path().stringByDeletingLastPathComponent;
  NSString* daemon = [directory stringByAppendingPathComponent:@DAEMON_NAME];
  if (!directory || access(daemon.UTF8String, X_OK) != 0) return false;

  char* arguments[] = { (char*)daemon.UTF8String, NULL };
  posix_spawnattr_t attributes;
  posix_spawnattr_init(&attributes);
  posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSID
                                        | POSIX_SPAWN_CLOEXEC_DEFAULT
                                        | POSIX_SPAWN_SETSIGDEF
                                        | POSIX_SPAWN_SETSIGMASK);
  // AppKit ignores SIGTERM, and a child inherits that. Without this reset the
  // daemon could never be stopped with a signal.
  sigset_t all_signals, no_signals;
  sigfillset(&all_signals);
  sigemptyset(&no_signals);
  posix_spawnattr_setsigdefault(&attributes, &all_signals);
  posix_spawnattr_setsigmask(&attributes, &no_signals);
  disclaim_responsibility(&attributes);

  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
  posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
  posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);

  pid_t pid;
  int result = posix_spawn(&pid, arguments[0], &actions, &attributes,
                           arguments, environ);
  posix_spawn_file_actions_destroy(&actions);
  posix_spawnattr_destroy(&attributes);
  if (result != 0) return false;

  dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
    while (waitpid(pid, NULL, 0) < 0 && errno == EINTR);
  });
  return true;
}

static void terminate_standalone_daemons(void) {
  int count = proc_listpids(PROC_UID_ONLY, getuid(), NULL, 0) / sizeof(pid_t);
  if (count <= 0) return;
  pid_t pids[count];
  count = proc_listpids(PROC_UID_ONLY, getuid(), pids, sizeof(pids)) / sizeof(pid_t);
  for (int i = 0; i < count; ++i) {
    char name[PROC_PIDPATHINFO_MAXSIZE];
    if (pids[i] > 0 && proc_name(pids[i], name, sizeof(name)) > 0
        && strcmp(name, DAEMON_NAME) == 0) {
      kill(pids[i], SIGTERM);
    }
  }
}

static bool daemon_start(void) {
  const char* label = service_label();
  if (!label) return spawn_daemon();
  if (service_loaded(label)) {
    launchctl("kickstart", NULL, service_target(label));
  } else {
    NSString* plist = [NSString stringWithFormat:@"Library/LaunchAgents/%s.plist", label];
    char* arguments[] = { "/bin/launchctl", "bootstrap",
                          (char*)[NSString stringWithFormat:@"gui/%u", getuid()].UTF8String,
                          (char*)home_path(plist).UTF8String, NULL };
    run_tool(arguments);
  }
  return true;
}

// A plain kill would not stop the service because launchd keeps it alive.
static void daemon_stop(void) {
  const char* label = service_label();
  if (label && service_loaded(label)) {
    launchctl("bootout", NULL, service_target(label));
  } else {
    terminate_standalone_daemons();
  }
}

// launchctl exit codes are unreliable here (bootout can report a slow shutdown
// as an error), so success means the daemon reached the requested state.
static bool daemon_perform(enum daemon_action action) {
  switch (action) {
    case DAEMON_START:
      return daemon_start() && wait_for_daemon_state(true);
    case DAEMON_STOP:
      daemon_stop();
      return wait_for_daemon_state(false);
    case DAEMON_RESTART: {
      const char* label = service_label();
      if (label && service_loaded(label)) {
        launchctl("kickstart", "-k", service_target(label));
        return wait_for_daemon_state(true);
      }
      daemon_stop();
      return wait_for_daemon_state(false)
             && daemon_start()
             && wait_for_daemon_state(true);
    }
  }
  return false;
}

struct option_choice {
  const char* title;
  const char* argument;
};

static const struct option_choice g_color_choices[] = {
  { "Auto", "color=auto" },
  { "Inverse", "color=inverse" },
  { "Auto Gradient", "color=auto-gradient" },
  { "Inverse Gradient", "color=inverse-gradient" }
};

static const struct option_choice g_style_choices[] = {
  { "Round", "style=round" },
  { "Uniform", "style=uniform" },
  { "Square", "style=square" }
};

static const struct option_choice g_width_choices[] = {
  { "2", "width=2.0" },
  { "4", "width=4.0" },
  { "5", "width=5.0" },
  { "6", "width=6.0" },
  { "8", "width=8.0" },
  { "10", "width=10.0" }
};

static const struct option_choice g_refresh_choices[] = {
  { "Off", "color_refresh=0" },
  { "500 ms", "color_refresh=500" },
  { "1 s", "color_refresh=1000" },
  { "2 s", "color_refresh=2000" },
  { "5 s", "color_refresh=5000" }
};

#define CHOICE_COUNT(choices) (sizeof(choices) / sizeof(*(choices)))

@interface MenuBarController : NSObject <NSApplicationDelegate, NSMenuDelegate>
@end

@implementation MenuBarController {
  NSStatusItem* _status_item;
  NSTimer* _timer;
  dispatch_queue_t _queue;
  struct menubar_options _saved;
  bool _running;
  bool _busy;
  NSString* _failure;
}

- (NSString*)optionsPath {
  return home_path(@".config/borders/menu-options");
}

- (NSString*)scriptPath {
  return home_path(@".config/borders/bordersrc");
}

- (NSString*)loginItemPath {
  return home_path(@"Library/LaunchAgents/" LOGIN_ITEM_LABEL ".plist");
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
  _queue = dispatch_queue_create("bordersbar.daemon", DISPATCH_QUEUE_SERIAL);
  menubar_options_load(&_saved, self.optionsPath.UTF8String);

  _status_item = [NSStatusBar.systemStatusBar
                   statusItemWithLength:NSSquareStatusItemLength];
  NSMenu* menu = [[NSMenu alloc] init];
  menu.autoenablesItems = NO;
  menu.delegate = self;
  _status_item.menu = menu;

  [self pollDaemon];
  _timer = [NSTimer timerWithTimeInterval:2.0
                                   target:self
                                 selector:@selector(pollDaemon)
                                 userInfo:nil
                                  repeats:YES];
  [NSRunLoop.mainRunLoop addTimer:_timer forMode:NSRunLoopCommonModes];
}

- (void)updateIcon {
  NSString* symbol = _running ? @"square.inset.filled" : @"square.dashed";
  NSImage* image = [NSImage imageWithSystemSymbolName:symbol
                             accessibilityDescription:@"Borders"];
  image.template = YES;
  _status_item.button.image = image;
  _status_item.button.title = image ? @"" : @"B";
}

// A starting daemon runs bordersrc right after registering, and later arguments
// win, so the saved choices are sent once that script has had time to finish.
- (void)sendSavedOptionsAfterStartup {
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 1 * NSEC_PER_SEC),
                 dispatch_get_main_queue(), ^{
    if (!self->_busy) daemon_send(&self->_saved);
  });
}

// The daemon cannot report its settings and may restart on its own (the service
// is kept alive), so the saved choices are sent whenever it comes back.
- (void)pollDaemon {
  bool running = daemon_running();
  if (running && !_running && !_busy) [self sendSavedOptionsAfterStartup];
  _running = running;
  [self updateIcon];
}

- (void)perform:(enum daemon_action)action failure:(NSString*)failure {
  if (_busy) return;
  _busy = true;
  _failure = nil;
  dispatch_async(_queue, ^{
    bool success = daemon_perform(action);
    dispatch_async(dispatch_get_main_queue(), ^{
      self->_busy = false;
      self->_running = daemon_running();
      self->_failure = success ? nil : failure;
      if (self->_running) [self sendSavedOptionsAfterStartup];
      [self updateIcon];
    });
  });
}

- (void)toggleDaemon:(id)sender {
  if (_running) [self perform:DAEMON_STOP failure:@"Could not stop borders"];
  else [self perform:DAEMON_START failure:@"Could not start borders"];
}

- (void)restartDaemon:(id)sender {
  [self perform:DAEMON_RESTART failure:@"Could not restart borders"];
}

- (void)setOption:(NSString*)argument {
  if (!menubar_options_set(&_saved, argument.UTF8String)) return;
  [NSFileManager.defaultManager
    createDirectoryAtPath:self.optionsPath.stringByDeletingLastPathComponent
    withIntermediateDirectories:YES attributes:nil error:NULL];
  _failure = menubar_options_save(&_saved, self.optionsPath.UTF8String)
             ? nil : @"Could not save settings";

  if (!_busy) {
    struct menubar_options single;
    menubar_options_clear(&single);
    menubar_options_set(&single, argument.UTF8String);
    daemon_send(&single);
  }
}

- (void)selectOption:(NSMenuItem*)item {
  [self setOption:item.representedObject];
}

- (void)chooseFixedColor:(id)sender {
  NSColorPanel* panel = NSColorPanel.sharedColorPanel;
  panel.showsAlpha = YES;
  panel.continuous = NO;
  panel.target = self;
  panel.action = @selector(colorPanelChanged:);
  [NSApp activateIgnoringOtherApps:YES];
  [panel orderFront:nil];
}

- (void)colorPanelChanged:(NSColorPanel*)panel {
  NSColor* color = [panel.color colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
  if (!color) return;
  uint32_t value = ((uint32_t)lround(color.alphaComponent * 255.0) << 24)
                 | ((uint32_t)lround(color.redComponent * 255.0) << 16)
                 | ((uint32_t)lround(color.greenComponent * 255.0) << 8)
                 |  (uint32_t)lround(color.blueComponent * 255.0);
  [self setOption:[NSString stringWithFormat:@"color=0x%08x", value]];
}

- (void)editScript:(id)sender {
  // `open -t` forces a text editor. The script is executable, so a plain open
  // would run it in Terminal instead.
  NSString* path = self.scriptPath;
  dispatch_async(_queue, ^{
    char* arguments[] = { "/usr/bin/open", "-t", (char*)path.UTF8String, NULL };
    run_tool(arguments);
  });
}

- (void)resetToScript:(id)sender {
  menubar_options_clear(&_saved);
  menubar_options_save(&_saved, self.optionsPath.UTF8String);
  // bordersrc only runs when the daemon starts.
  if (_running) [self restartDaemon:sender];
}

- (void)toggleLoginItem:(id)sender {
  NSString* path = self.loginItemPath;
  NSFileManager* files = NSFileManager.defaultManager;
  if ([files fileExistsAtPath:path]) {
    _failure = [files removeItemAtPath:path error:NULL]
               ? nil : @"Could not update Launch at Login";
    return;
  }

  NSString* executable = executable_path();
  NSDictionary* job = @{
    @"Label": @LOGIN_ITEM_LABEL,
    @"ProgramArguments": executable ? @[ executable ] : @[],
    @"RunAtLoad": @YES,
    @"LimitLoadToSessionType": @"Aqua",
    @"ProcessType": @"Interactive"
  };
  [files createDirectoryAtPath:path.stringByDeletingLastPathComponent
    withIntermediateDirectories:YES attributes:nil error:NULL];
  _failure = executable && [job writeToFile:path atomically:YES]
             ? nil : @"Could not update Launch at Login";
}

- (NSMenuItem*)addItemTo:(NSMenu*)menu title:(NSString*)title action:(SEL)action {
  NSMenuItem* item = [menu addItemWithTitle:title action:action keyEquivalent:@""];
  item.target = action ? self : nil;
  item.enabled = action != NULL;
  return item;
}

- (NSMenu*)addSubmenuTo:(NSMenu*)menu
                  title:(NSString*)title
                choices:(const struct option_choice*)choices
                  count:(size_t)count
              displayed:(const struct menubar_options*)displayed {
  NSMenu* submenu = [[NSMenu alloc] init];
  submenu.autoenablesItems = NO;
  for (size_t i = 0; i < count; ++i) {
    NSMenuItem* item = [self addItemTo:submenu
                                 title:@(choices[i].title)
                                action:@selector(selectOption:)];
    item.representedObject = @(choices[i].argument);
    item.state = menubar_options_matches(displayed, choices[i].argument)
                 ? NSControlStateValueOn : NSControlStateValueOff;
  }
  NSMenuItem* parent = [self addItemTo:menu title:title action:NULL];
  parent.enabled = YES;
  parent.submenu = submenu;
  return submenu;
}

- (NSString*)statusTitle {
  if (_busy) return @"Borders: Working…";
  if (_failure) return _failure;
  return _running ? @"Borders: Running" : @"Borders: Stopped";
}

- (void)menuNeedsUpdate:(NSMenu*)menu {
  [menu removeAllItems];

  // Options never chosen here are shown as bordersrc sets them. This is only an
  // estimate: the daemon cannot report what it is really using.
  struct menubar_options displayed;
  menubar_options_clear(&displayed);
  menubar_options_scan_script(&displayed, self.scriptPath.UTF8String);
  menubar_options_merge(&displayed, &_saved);

  [self addItemTo:menu title:self.statusTitle action:NULL];
  [menu addItem:NSMenuItem.separatorItem];

  NSMenuItem* toggle = [self addItemTo:menu
                                 title:_running ? @"Stop" : @"Start"
                                action:@selector(toggleDaemon:)];
  toggle.enabled = !_busy;
  NSMenuItem* restart = [self addItemTo:menu
                                  title:@"Restart"
                                 action:@selector(restartDaemon:)];
  restart.enabled = !_busy && _running;
  [menu addItem:NSMenuItem.separatorItem];

  NSMenu* colors = [self addSubmenuTo:menu
                                title:@"Color"
                              choices:g_color_choices
                                count:CHOICE_COUNT(g_color_choices)
                            displayed:&displayed];
  [colors addItem:NSMenuItem.separatorItem];
  NSMenuItem* fixed = [self addItemTo:colors
                                title:@"Fixed Color…"
                               action:@selector(chooseFixedColor:)];
  fixed.state = strncmp(displayed.values[MENUBAR_OPTION_COLOR], "color=0x", 8) == 0
                ? NSControlStateValueOn : NSControlStateValueOff;

  [self addSubmenuTo:menu title:@"Style" choices:g_style_choices
               count:CHOICE_COUNT(g_style_choices) displayed:&displayed];
  [self addSubmenuTo:menu title:@"Width" choices:g_width_choices
               count:CHOICE_COUNT(g_width_choices) displayed:&displayed];
  [self addSubmenuTo:menu title:@"Color Refresh" choices:g_refresh_choices
               count:CHOICE_COUNT(g_refresh_choices) displayed:&displayed];
  [menu addItem:NSMenuItem.separatorItem];

  NSFileManager* files = NSFileManager.defaultManager;
  NSMenuItem* edit = [self addItemTo:menu
                               title:@"Edit bordersrc…"
                              action:@selector(editScript:)];
  edit.enabled = [files fileExistsAtPath:self.scriptPath];
  NSMenuItem* reset = [self addItemTo:menu
                                title:@"Reset to bordersrc"
                               action:@selector(resetToScript:)];
  reset.enabled = !_busy && !menubar_options_is_empty(&_saved);
  NSMenuItem* login = [self addItemTo:menu
                                title:@"Launch at Login"
                               action:@selector(toggleLoginItem:)];
  login.state = [files fileExistsAtPath:self.loginItemPath]
                ? NSControlStateValueOn : NSControlStateValueOff;
  [menu addItem:NSMenuItem.separatorItem];

  NSMenuItem* quit = [menu addItemWithTitle:@"Quit Menu"
                                     action:@selector(terminate:)
                              keyEquivalent:@""];
  quit.target = NSApp;
}
@end

// A second copy would add a second icon. The lock is released when the process
// ends and is not inherited by the daemon (O_CLOEXEC).
static bool acquire_single_instance_lock(void) {
  NSString* path = home_path(@".config/borders/.bordersbar.lock");
  [NSFileManager.defaultManager
    createDirectoryAtPath:path.stringByDeletingLastPathComponent
    withIntermediateDirectories:YES attributes:nil error:NULL];
  int descriptor = open(path.UTF8String, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
  return descriptor >= 0 && flock(descriptor, LOCK_EX | LOCK_NB) == 0;
}

int main(int argc, char** argv) {
  @autoreleasepool {
    if (!acquire_single_instance_lock()) {
      fprintf(stderr, "[!] bordersbar is already running.\n");
      return 1;
    }

    NSApplication* application = NSApplication.sharedApplication;
    application.activationPolicy = NSApplicationActivationPolicyAccessory;
    MenuBarController* controller = [[MenuBarController alloc] init];
    application.delegate = controller;
    [application run];
  }
  return 0;
}
