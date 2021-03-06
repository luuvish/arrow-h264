# -*- coding: utf-8 -*-

'''
================================================================================

  This confidential and proprietary software may be used only
 as authorized by a licensing agreement from Thumb o'Cat Inc.
 In the event of publication, the following notice is applicable:

      Copyright (C) 2013 - 2014 Thumb o'Cat
                    All right reserved.

  The entire notice above must be reproduced on all authorized copies.

================================================================================

 File      : vc1.py
 Author(s) : Luuvish
 Version   : 2.0
 Revision  :
     2.0 May 13, 2014    Executor classify

================================================================================
'''

__all__ = ('models', 'suites')

__version__ = '2.0.0'

from os.path import join

from . import rootpath
from ..model.coda960 import Coda960
from ..model.ffmpeg import FFmpeg
from ..model.smpte_vc1 import SMPTEVc1
from ..model.wmfdecode import WmfDecode


models = (Coda960, FFmpeg, SMPTEVc1, WmfDecode)

suites = (
    {
        'suite' : 'decode-vc1-wmfdecode',
        'model' : 'wmfdecode',
        'codec' : 'vc1',
        'action': 'decode',
        'stdout': 'vc1-wmfdecode.log',
        'srcdir': join(rootpath, 'test/stream/vc1'),
        'outdir': join(rootpath, 'test/image/vc1'),
        'includes': (
            'artificial6/*_Main_Progressive_*',
            'artificial7/*',
            'smpte-vc1/*',
            'conf-vc1/*',
            'conf-wmv9/*',
            'stress/*'
        ),
        'excludes': ()
    },
    {
        'suite' : 'decode-vc1-smpte',
        'model' : 'smpte-vc1',
        'codec' : 'vc1',
        'action': 'decode',
        'stdout': 'vc1-smpte.log',
        'srcdir': join(rootpath, 'test/stream/vc1'),
        'outdir': join(rootpath, 'test/image/vc1'),
        'includes': (
            'artificial6/*_Main_Progressive_*',
            'artificial7/*',
            'smpte-vc1/*',
            'conf-vc1/*',
            'conf-wmv9/*',
            'stress/*'
        ),
        'excludes': ()
    },
    {
        'suite' : 'digest-vc1-coda960',
        'model' : 'coda960',
        'codec' : 'vc1',
        'action': 'digest',
        'stdout': 'vc1-coda960.log',
        'srcdir': join(rootpath, 'test/stream/vc1'),
        'outdir': join(rootpath, 'test/digest/vc1'),
        'includes': (
            'artificial6/*_Main_Progressive_*',
            'artificial7/*',
            'smpte-vc1/*',
            'conf-vc1/*',
            'conf-wmv9/*',
            'stress/*'
        ),
        'excludes': ()
    },
    {
        'suite' : 'compare-vc1-coda960',
        'model' : 'coda960',
        'codec' : 'vc1',
        'action': 'compare',
        'stdout': 'vc1-coda960.log',
        'srcdir': join(rootpath, 'test/stream/vc1'),
        'outdir': join(rootpath, 'test/digest/vc1'),
        'includes': (
            'artificial6/*_Main_Progressive_*',
            'artificial7/*',
            'smpte-vc1/*',
            'conf-vc1/*',
            'conf-wmv9/*',
            'stress/*'
        ),
        'excludes': ()
    }
)
