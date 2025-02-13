# Required for Python to search this directory for module files

# Keep this file free of any code or import statements that could
# cause either an error to occur or a log message to be logged.
# This ensures that calling code can import initialization code from
# webkitpy before any errors or log messages due to code in this file.
# Initialization code can include things like version-checking code and
# logging configuration code.
#
# We do not execute any version-checking code or logging configuration
# code in this file so that callers can opt-in as they want.  This also
# allows different callers to choose different initialization code,
# as necessary.

import os
import sys

# We always want the real system version
os.environ['SYSTEM_VERSION_COMPAT'] = '0'

libraries = os.path.join(os.path.abspath(os.path.dirname(os.path.dirname(__file__))), 'libraries')
sys.path.insert(0, os.path.join(libraries, 'webkitcorepy'))

if sys.platform == 'darwin':
    is_root = not os.getuid()
    does_own_libraries = os.stat(libraries).st_uid == os.getuid()
    if (is_root or not does_own_libraries):
        libraries = os.path.expanduser('~/Library/webkitpy')

from webkitcorepy import AutoInstall, Package, Version
AutoInstall.set_directory(os.path.join(libraries, 'autoinstalled', 'python-{}'.format(sys.version_info[0])))

AutoInstall.register(Package('atomicwrites', Version(1, 4, 0)))
AutoInstall.register(Package('attr', Version(19, 1, 0), pypi_name='attrs'))
AutoInstall.register(Package('configparser', Version(4, 0, 2)))
AutoInstall.register(Package('contextlib2', Version(0, 6, 0)))
AutoInstall.register(Package('coverage', Version(5, 2, 1)))
AutoInstall.register(Package('funcsigs', Version(1, 0, 2)))
AutoInstall.register(Package('importlib_metadata', Version(1, 7, 0)))
AutoInstall.register(Package('more_itertools', Version(5, 0, 0), pypi_name='more-itertools'))
AutoInstall.register(Package('genshi', Version(0, 7, 3), pypi_name='Genshi'))
AutoInstall.register(Package('html5lib', Version(1, 1)))
AutoInstall.register(Package('keyring', Version(7, 3, 1)))
AutoInstall.register(Package('logilab.common', Version(0, 58, 1), pypi_name='logilab-common', aliases=['logilab']))
AutoInstall.register(Package('logilab.astng', Version(0, 24, 1), pypi_name='logilab-astng', aliases=['logilab']))
AutoInstall.register(Package('mechanize', Version(0, 4, 5)))
AutoInstall.register(Package('mozprocess', Version(1, 2, 0)))
AutoInstall.register(Package('mozlog', Version(6, 1)))
AutoInstall.register(Package('mozterm', Version(1, 0, 0)))
AutoInstall.register(Package('pathlib2', Version(2, 3, 5)))
AutoInstall.register(Package('pluggy', Version(0, 13, 1)))
AutoInstall.register(Package('py', Version(1, 9, 0)))
AutoInstall.register(Package('pytest_timeout', Version(1, 4, 2), pypi_name='pytest-timeout'))
# Pytest held to 3.x due to WPT webdriver compatibility
AutoInstall.register(Package('pytest', Version(3, 6, 2), implicit_deps=['pytest_timeout']))
AutoInstall.register(Package('pycodestyle', Version(2, 5, 0)))
AutoInstall.register(Package('pylint', Version(0, 25, 2)))
AutoInstall.register(Package('scandir', Version(1, 10, 0)))
AutoInstall.register(Package('selenium', Version(3, 141, 0)))
AutoInstall.register(Package('toml', Version(0, 10, 1)))
AutoInstall.register(Package('wcwidth', Version(0, 2, 5)))
AutoInstall.register(Package('webencodings', Version(0, 5, 1)))
AutoInstall.register(Package('zipp', Version(1, 2, 0)))
AutoInstall.register(Package('zope.interface', Version(5, 1, 0), aliases=['zope'], pypi_name='zope-interface'))

AutoInstall.register(Package('webkitscmpy', Version(0, 0, 1)), local=True)
