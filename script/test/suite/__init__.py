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

 File      : suite.py
 Author(s) : Luuvish
 Version   : 2.0
 Revision  :
     2.0 May 12, 2014    Executor classify

================================================================================
'''

__all__ = ('rootpath', )

__version__ = '2.0.0'

from os.path import join, normpath, dirname

rootpath = normpath(join(dirname(__file__), '../../..'))
