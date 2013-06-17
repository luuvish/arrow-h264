
/*!
 ***************************************************************************
 *
 * \file fmo.h
 *
 * \brief
 *    Support for Flexilble Macroblock Ordering (FMO)
 *
 * \date
 *    19 June, 2002
 *
 * \author
 *    Stephan Wenger   stewe@cs.tu-berlin.de
 **************************************************************************/

#ifndef _FMO_H_
#define _FMO_H_

#ifdef __cplusplus
extern "C" {
#endif

struct slice_t;

extern int fmo_init (VideoParameters *p_Vid, struct slice_t *pSlice);
extern int FmoFinit (VideoParameters *p_Vid);

extern int FmoGetNumberOfSliceGroup(VideoParameters *p_Vid);
extern int FmoGetLastMBOfPicture   (VideoParameters *p_Vid);
extern int FmoGetLastMBInSliceGroup(VideoParameters *p_Vid, int SliceGroup);
extern int FmoGetSliceGroupId      (VideoParameters *p_Vid, int mb);
extern int FmoGetNextMBNr          (VideoParameters *p_Vid, int CurrentMbNr);

#ifdef __cplusplus
}
#endif

#endif
