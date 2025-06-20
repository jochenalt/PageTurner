import sys
import termios
import fcntl
import os

# Store original terminal settings
_fd = sys.stdin.fileno()
_old_termios = termios.tcgetattr(_fd)
_old_flags = fcntl.fcntl(_fd, fcntl.F_GETFL)
_buffer = []

def init_kbhit():
    """Enable non-blocking keyboard input (raw mode)."""
    # Disable canonical mode and echo
    new_term = termios.tcgetattr(_fd)
    new_term[3] &= ~(termios.ICANON | termios.ECHO)
    termios.tcsetattr(_fd, termios.TCSANOW, new_term)
    # Enable non-blocking mode on stdin
    fcntl.fcntl(_fd, fcntl.F_SETFL, _old_flags | os.O_NONBLOCK)

def restore_kbhit():
    """Restore terminal to its original settings."""
    termios.tcsetattr(_fd, termios.TCSANOW, _old_termios)
    fcntl.fcntl(_fd, fcntl.F_SETFL, _old_flags)

def kbhit():
    """Return True if a keypress is waiting, False otherwise."""
    try:
        ch = sys.stdin.read(1)
        if ch:
            _buffer.append(ch)
            return True
    except (IOError, OSError):
        pass
    return False

def getch():
    """Read a single character (consumes buffered key if present)."""
    if _buffer:
        return _buffer.pop(0)
    return sys.stdin.read(1)