from setuptools import setup, find_packages
import os, glob

import sys

setup(name = 'mod',
      version = '0.99.8',
      description = 'MOD',
      long_description = 'MOD - Musician Operated Device - User Interface server and libraries',
      author = "Hacklab and AGR",
      author_email = "lhfagundes@hacklab.com.br",
      license = "GPLv3",
      packages = find_packages(),
      entry_points = {
          'console_scripts': [
              'mod-ui = mod.webserver:run',
              ]
          },
      scripts = [
      ],
      data_files=[  (os.path.join(sys.prefix, 'share/mod/html'), glob.glob('html/*.html')),
                    (os.path.join(sys.prefix, 'share/mod/html/include'), glob.glob('html/include/*.html')),
                    (os.path.join(sys.prefix, 'share/mod/html/resources'), glob.glob('html/resources/*.html')),
                    (os.path.join(sys.prefix, 'share/mod/html/resources/pedals'), glob.glob('html/resources/pedals/*.png')),
                    (os.path.join(sys.prefix, 'share/mod/html/resources/pedals'), glob.glob('html/resources/pedals/*.css')),
                    (os.path.join(sys.prefix, 'share/mod/html/resources/pedals'), glob.glob('html/resources/pedals/*.html')),
                    (os.path.join(sys.prefix, 'share/mod/html/resources/templates'), glob.glob('html/resources/templates/*.html')),
                    (os.path.join(sys.prefix, 'share/mod/html/img'), glob.glob('html/img/*.png')),
                    (os.path.join(sys.prefix, 'share/mod/html/img'), glob.glob('html/img/*.jpg')),
                    (os.path.join(sys.prefix, 'share/mod/html/img'), glob.glob('html/img/*.jpeg')),
                    (os.path.join(sys.prefix, 'share/mod/html/img/cloud'), glob.glob('html/img/cloud/*.png')),
                    (os.path.join(sys.prefix, 'share/mod/html/img/cloud'), glob.glob('html/img/cloud/*.jpg')),
                    (os.path.join(sys.prefix, 'share/mod/html/img/cloud'), glob.glob('html/img/cloud/*.jpeg')),
                    (os.path.join(sys.prefix, 'share/mod/html/css'), glob.glob('html/css/*.css')),
                    (os.path.join(sys.prefix, 'share/mod/html/js'), glob.glob('html/js/*.js')),
                    (os.path.join(sys.prefix, 'share/mod/html/js/lib'), glob.glob('html/js/lib/*.js')),
                    (os.path.join(sys.prefix, 'share/mod'), ['screenshot.js']),
          ],
      install_requires = ['tornado', 'whoosh'],
      classifiers = [
          'Intended Audience :: Developers',
          'Natural Language :: English',
          'Operating System :: OS Independent',
          'Programming Language :: Python',
        ],
      url = 'http://moddevices.com/',
)
