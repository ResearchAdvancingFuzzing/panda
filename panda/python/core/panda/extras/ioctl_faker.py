import sys
import ctypes
import logging

# Only for logger, should probably move it to a separate file
from panda.extras.file_hook import FileHook

# TODO: OSI integration for process pid, name, and file path
# TODO: Ability to fake buffers for specific commands

# Default config (x86, x86-64, ARM, AArch 64)
config = sys.modules[__name__]
config.IOC_TYPE_BITS = 8
config.IOC_CMD_BITS  = 8
config.IOC_SIZE_BITS = 14
config.IOC_DIR_BITS  = 2
config.SUCCESS_RET   = 0

class IoctlCmdBits(ctypes.LittleEndianStructure):

    '''
    Pythonic bit-packing without 3rd party libs
    '''

    _fields_ = [
        ("type_num", ctypes.c_uint8, config.IOC_TYPE_BITS),
        ("cmd_num", ctypes.c_uint8, config.IOC_CMD_BITS),
        ("arg_size", ctypes.c_uint16, config.IOC_SIZE_BITS),
        ("direction", ctypes.c_uint8, config.IOC_DIR_BITS),
    ]

class IoctlCmdUnion(ctypes.Union):

    '''
    Python alternative to bit-packed struct
    '''

    _fields_ = [
        ("bits", IoctlCmdBits),
        ("asUnsigned32", ctypes.c_uint32),
    ]

class IoctlCmd(IoctlCmdUnion):

    '''
    Human-readable ioctl command
    '''

    def __str__(self):

        if self.bits.direction == 0x0:
            direction = "IO"
        elif self.bits.direction == 0x1:
            direction = "IOW"
        elif self.bits.direction == 0x2:
            direction = "IOR"
        elif self.bits.direction == 0x3:
            direction = "IOWR"
        else:
            raise RuntimeError("Invalid ioctl direction decode!")

        return "dir={},arg_size={:x},cmd={:x},type={:x}".format(
            direction,
            self.bits.arg_size,
            self.bits.cmd_num,
            self.bits.type_num
        )

    def __eq__(self, other):

        return (
            self.__class__ == other.__class__ and
            self.asUnsigned32 == other.asUnsigned32
        )

    def __hash__(self):

        return hash(self.asUnsigned32)

class Ioctl():

    '''
    Unpacked ioctl command with optional buffer
    '''

    def __init__(self, panda, cpu, cmd, guest_ptr):

        self.cmd = IoctlCmd()
        self.cmd.asUnsigned32 = cmd
        self.original_ret_code = None
        if (self.cmd.bits.arg_size > 0):
            try:
                self.has_buf = True
                self.guest_ptr = guest_ptr
                self.guest_buf = panda.virtual_memory_read(cpu, self.guest_ptr, self.cmd.bits.arg_size)
            except ValueError:
                raise RuntimeError("Failed to read guest buffer: ioctl({})".format(str(self.cmd)))
        else:
            self.has_buf = False
            self.guest_ptr = None
            self.guest_buf = None

    def set_ret_code(self, code):

        self.original_ret_code = code

    def __str__(self):

        if (self.guest_ptr == None):
            return "ioctl({}) -> {}".format(
                str(self.cmd),
                self.original_ret_code
            )
        else:
            return "ioctl({},ptr={:08x},buf={}) -> {}".format(
                str(self.cmd),
                self.guest_ptr,
                self.guest_buf,
                self.original_ret_code
            )

    def __eq__(self, other):

        return (
            self.__class__ == other.__class__ and
            self.cmd == other.cmd and
            self.has_buf == other.has_buf and
            self.guest_ptr == other.guest_ptr and
            # TODO: iterate buf to compare every byte? Or does this work?
            self.guest_buf == other.guest_buf
        )

    def __hash__(self):

        return hash((self.cmd, self.has_buf, self.guest_ptr, self.guest_buf))

class IoctlFaker():

    '''
    Interpose ioctl() syscall returns, forcing successes for any failures to simulate missing drivers/peripherals.
    Bin all returns into failures (needed forcing) and successes, store for later retrival/analysis.
    '''

    def __init__(self, panda):

        self._panda = panda
        self._panda.load_plugin("syscalls2")

        self._logger = logging.getLogger('panda.hooking')
        self._logger.setLevel(logging.DEBUG)

        # Save runtime memory with sets instead of lists (no duplicates)
        self._fail_returns = set()
        self._success_returns = set()

        # PPC (other arches use the default config)
        if self._panda.arch == "ppc":
            config.IOC_SIZE_BITS = 13
            config.IOC_DIR_BITS  = 3

        # Force success returns for missing drivers/peripherals
        @self._panda.ppp("syscalls2", "on_sys_ioctl_return")
        def on_sys_ioctl_return(cpu, pc, fd, cmd, arg):

            ioctl = Ioctl(self._panda, cpu, cmd, arg)
            ioctl.set_ret_code(self._panda.from_unsigned_guest(cpu.env_ptr.regs[0]))

            if (ioctl.original_ret_code != config.SUCCESS_RET):
                self._fail_returns.add(ioctl)
                cpu.env_ptr.regs[0] = 0
                if ioctl.has_buf:
                    self._logger.warning("Forcing success return for data-containing {}".format(ioctl))
                else:
                    self._logger.info("Forcing success return for data-less {}".format(ioctl))
            else:
                self._success_returns.add(ioctl)

    def _get_returns(self, source, with_buf_only):

        if with_buf_only:
            return list(filter(lambda i: (i.has_buf == True), source))
        else:
            return source

    def get_forced_returns(self, with_buf_only = False):

        return self._get_returns(self._fail_returns, with_buf_only)

    def get_unmodified_returns(self, with_buf_only = False):

        return self._get_returns(self._success_returns, with_buf_only)

if __name__ == "__main__":

    '''
    Bash will issue ioctls on /dev/ttys0 - this is just a simple test to make sure they're being captured
    '''

    from panda import blocking, Panda

    # No arguments, i386. Otherwise argument should be guest arch
    generic_type = sys.argv[1] if len(sys.argv) > 1 else "i386"
    panda = Panda(generic=generic_type)

    def print_list_elems(l):

        if not l:
            print("None")
        else:
            for e in l:
                print(e)

    @blocking
    def run_cmd():

        # Setup faker
        ioctl_faker = IoctlFaker(panda)

        print("\nRunning \'ls -l\' to ensure ioctl() capture is working...\n")

        # First revert to root snapshot, then type a command via serial
        panda.revert_sync("root")
        panda.run_serial_cmd("cd / && ls -l")

        # Check faker's results
        faked_rets = ioctl_faker.get_forced_returns()
        normal_rets = ioctl_faker.get_unmodified_returns()

        print("{} faked ioctl returns:".format(len(faked_rets)))
        print_list_elems(faked_rets)
        print("\n")

        print("{} normal ioctl returns:".format(len(normal_rets)))
        print_list_elems(normal_rets)
        print("\n")

        # Cleanup
        panda.end_analysis()

    panda.queue_async(run_cmd)
    panda.run()
