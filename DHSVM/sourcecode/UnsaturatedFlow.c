/*
 * SUMMARY:      UnsaturatedFlow.c - Calculate the unsaturated flow
 * USAGE:        Part of DHSVM
 *
 * AUTHOR:       Bart Nijssen and Mark Wigmosta (*)
 * ORG:          University of Washington, Department of Civil Engineering
 * E-MAIL:       nijssen@u.washington.edu
 * ORIG-DATE:    Apr-1996
 * DESCRIPTION:  Calculate the unsaturated flow in the soil column (vertical
 *               flow)
 * DESCRIP-END.
 * FUNCTIONS:    UnsaturatedFlow()
 * COMMENTS: (*) Mark Wigmosta, Batelle Pacific Northwest Laboratories,
 *               ms_wigmosta@pnl.gov
 * $Id: UnsaturatedFlow.c,v 1.12 2004/05/04 19:38:59 colleen Exp $
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "constants.h"
#include "settings.h"
#include "functions.h"
#include "soilmoisture.h"
#include <assert.h>

 /*****************************************************************************
   Function name: UnsaturatedFlow()

   Purpose      : Calculate the unsaturated flow in the soil column, and
                  adjust the moisture in each soil layer

   Required     :
     int Dt             - Time step (seconds)
     float DX           - Grid cell width (m)
     float DY           - Grid cell width (m)
     float Infiltration - Amount of infiltration entering the top of the soil
                          column (m)
     float RoadbedInfiltration - Amount of infiltration through the roadbed(m)
     float SatFlow      - Amount of saturated flow entering the soil column
                          from neighbouring pixels (m)
     int NSoilLayers    - Number of soil layers
     float TotalDepth   - Total depth of the soil profile (m)
     float Area         - Area of channel or road surface (m)
     float *RootDepth   - Depth of each of the soil layers (m)
     float *Ks          - Vertical saturated hydraulic conductivity in each
                          soil layer (m/s)
     float *PoreDist    - Pore size distribution index for each soil layer
     float *Porosity    - Porosity of each soil layer
     float *FCap        - Field capacity of each soil layer
     float *Perc        - Amount of water percolating from each soil layer to
                          the layer below (m)
     float *PercArea    - Area of the bottom of each soil layer as a fraction
                          of the gridcell area DX*DY
     float *Adjust      - Correction for each layer for loss of soil storage
                          due to channel/road-cut.  Multiplied with RootDepth
                          to give the layer thickness for use in calculating
                          soil moisture
     int CutBankZone    - Number of the soil layer containing the bottom of
                          the cut-bank
     float BankHeight   - Distance from ground surface to channel bed or
                          bottom of road-cut (m)

   Returns      : void

   Modifies     :
     float *TableDepth - Depth of the water table below the ground surface (m)
     float *RunOff     - Amount of runoff produced at the pixel (m)
     float *Moist      - Moisture content in each soil layer

   Comments     :
     Sources:
     Bras, R. A., Hydrology, an introduction to hydrologic science, Addisson
         Wesley, Inc., Reading, etc., 1990.
     Wigmosta, M. S., L. W. Vail, and D. P. Lettenmaier, A distributed
         hydrology-vegetation model for complex terrain, Water Resour. Res.,
         30(6), 1665-1679, 1994.

     This function is based on Wigmosta et al [1994], and assumes a unit
     hydraulic gradient in the unsaturated zone.  This implies a
     steady-state situation and uniform moisture distribution.  This is not
     unreasonable for situations where the groundwater is fairly deep
     [Bras, 1990], but may be unrealistic for the boreal forest system, or
     other similar situations where the groundwater table is relatively close
     to the surface. The current formulation does not allow for upward
     movement of water in the unsaturated zone, by capillary and matrix
     forces.  No unsaturated flux is assumed to occur if the water content
     drops below the field capacity.

     The unsaturated hydraulic conductivity is calculated using the
     Brooks-Corey equation see for example Wigmosta et al. [1994], eq.41

     The calculated amount of drainage is averaged with the amount calculated
     for the previous timestep, see eq. 42 [Wigmosta et al., 1994].

     The residual moisture content is assumed to be zero (easy to adapt if
     needed).

     If the amount of soil moisture in a layer after drainage still
     exceeds the porosity for that layer, this additional amount of water is
     added to the drainage to the next layer.

     CHANGES:

     Changes have been made to account for the potental loss of soil storage
     in a grid cell due to a road-cut or channel.  Correction coefficents are
     calculated in AdjustStorage() and CutBankGeometry()
 *****************************************************************************/
void UnsaturatedFlow(int Dt, float DX, float DY, float Infiltration,
  float RoadbedInfiltration, float SatFlow, int NSoilLayers,
  float TotalDepth, float Area, float *RootDepth, float *Ks,
  float *PoreDist, float *Porosity, float *FCap,
  float *Perc, float *PercArea, float *Adjust,
  int CutBankZone, float BankHeight, float *TableDepth,
  float *Runoff, float *Moist, int InfiltOption)
{
  float DeepDrainage;	/* amount of drainage from the lowest root
                           zone to the layer below it (m) */
  float DeepLayerDepth;	/* depth of the layer below the deepest
                           root layer */
  float Drainage;		/* amount of water drained from each soil
                           layer during the current timestep */
  float Exponent;		/* Brooks-Corey exponent */
  float FieldCapacity;	/* amount of water in soil at field capacity (m) */
  float MaxSoilWater;	/* maximum allowable amount of soil moiture
                           in each layer (m) */
  float SoilWater;		/* amount of water in each soil layer (m) */
  int i;			    /* counter */

  DeepLayerDepth = TotalDepth;
  for (i = 0; i < NSoilLayers; i++)
    DeepLayerDepth -= RootDepth[i];

  /* first take care of infiltration through the roadbed/channel, then through the
     remaining surface */
  if (*TableDepth <= BankHeight)  /* watertable above road/channel surface */
    *Runoff += RoadbedInfiltration;
  else {
    if (CutBankZone == NSoilLayers) {
      Moist[NSoilLayers] += RoadbedInfiltration / (DeepLayerDepth * Adjust[NSoilLayers]);
    }
    else if (CutBankZone >= 0) {
      Moist[CutBankZone] += RoadbedInfiltration / (RootDepth[CutBankZone] * Adjust[CutBankZone]);
    }
  }
  if (*TableDepth <= 0) { /* watertable above surface */
    *Runoff += Infiltration;
    if (InfiltOption == DYNAMIC)
      Infiltration = 0.;
  }
  else {
    Moist[0] += Infiltration / (RootDepth[0] * Adjust[0]);
  }


  for (i = 0; i < NSoilLayers; i++) {
    /* No movement if soil moisture is below field capacity */
    if (Moist[i] > FCap[i]) {
      Exponent = 2.0 / PoreDist[i] + 3.0;
      if (Moist[i] > Porosity[i])
        /* this can happen because the moisture content can exceed the
         porosity the way the algorithm is implemented */
        Drainage = Ks[i];
      else
        Drainage =
        Ks[i] * pow((double)(Moist[i] / Porosity[i]), (double)Exponent);

      Drainage *= Dt;
      Perc[i] = 0.5 * (Perc[i] + Drainage) * PercArea[i];

      MaxSoilWater = RootDepth[i] * Porosity[i] * Adjust[i];
      SoilWater = RootDepth[i] * Moist[i] * Adjust[i];
      FieldCapacity = RootDepth[i] * FCap[i] * Adjust[i];

      /* No unsaturated flow if the moisture content drops below field capacity */
      if ((SoilWater - Perc[i]) < FieldCapacity)
        Perc[i] = SoilWater - FieldCapacity;

      /* WORK IN PROGRESS */
      /* If the moisture content is greater than the porosity add the additional
      soil moisture to the percolation */
      SoilWater -= Perc[i];
      if (SoilWater > MaxSoilWater)
        Perc[i] += SoilWater - MaxSoilWater;

      /* Adjust the moisture content in the current layer, and the layer immediately below it */
      Moist[i] -= Perc[i] / (RootDepth[i] * Adjust[i]);
      if (i < (NSoilLayers - 1))
        Moist[i + 1] += Perc[i] / (RootDepth[i + 1] * Adjust[i + 1]);
    }
    else
      Perc[i] = 0.0;

    /* convert back to straight 1-d flux */
    Perc[i] /= PercArea[i];
  }

  DeepDrainage = (Perc[NSoilLayers - 1] * PercArea[NSoilLayers - 1]);

  Moist[NSoilLayers] += DeepDrainage / (DeepLayerDepth * Adjust[NSoilLayers]);

  /* Calculate the depth of the water table based on the soil moisture
     profile and adjust the soil moisture profile, to assure that the soil
     moisture is never more than the maximum allowed soil moisture amount,
     i.e. the porosity.  A negative water table depth means that the water is
     ponding on the surface.  This amount of water becomes surface Runoff */
  *TableDepth = WaterTableDepth(NSoilLayers, TotalDepth, RootDepth, Porosity,
    FCap, Adjust, Moist);

  if (*TableDepth < 0.0) {
    *Runoff += -(*TableDepth);
    if (InfiltOption == DYNAMIC) {
      if (Infiltration > -(*TableDepth))
        Infiltration += *TableDepth;
      else
        Infiltration = 0.;
    }
    *TableDepth = 0.0;
  }
}
/*****************************************************************************
Function name: DistributeSatFlow()
*****************************************************************************/

void DistributeSatflow(int Dt, float DX, float DY, float SatFlow, int NSoilLayers,
  float TotalDepth, float *RootDepth, float *Ks, float *Porosity, float *FCap,
  float *Perc, float *PercArea, float *Adjust, float *TableDepth,
  float *Runoff, float *Moist)
{
  int i;			        /* counter */
  float DeepLayerDepth;		/* depth of the layer below the deepest root layer */

  /*Following variables adde by Zhuoran*/
  float DeepFCap;		    /* field capacity of the layer below the
                               deepest root layer */
  float DeepPorosity;		/* porosity of the layer below the deepest root layer */
  float DeepExcessFCap;
  float DeepAvaWater;
  float AvaWater;
  float DeepWaterGap;
  float WaterGap;
  float ExtracWater;
  float DeepExtracWater;
  float Depth;
  float MoistureTransfer;

  DeepPorosity = Porosity[NSoilLayers - 1];
  DeepFCap = FCap[NSoilLayers - 1];

  /*end of adding variable*/

  DeepLayerDepth = TotalDepth;
  for (i = 0; i < NSoilLayers; i++)
    DeepLayerDepth -= RootDepth[i];

  /* Added 06/09/2016 by Zhuoran Duan(zhuoran.duan@pnnl.gov) */
  /* When calculate outflow, water from all sacurated zone was accounted for outflow but
  only deep soil water was extracted. This would lead to a negative soil moisture in
  deep soil while the layer above it remains saturated. In order to avoid negative deep
  soil moisture, here I add a loop to redistribution of water extraction. The out flow starts
  from the deep layer, extract the excess water lager than field capacity. While there's no
  enough water from the current layer, extract water from one layer above it until it reaches
  water table surface. */

  /*New algorithm for SatFlow, remove water from top layer to bottom layer*/

  Depth = 0.0;
  if (SatFlow < 0.0) {
    ///* printf("SatFlow before distribution is %.6f\n",SatFlow);*/
    for (i = 0; i < NSoilLayers && Depth < TotalDepth; i++) {
      AvaWater = 0.0;
      ExtracWater = 0.0;
      if (RootDepth[i] < (TotalDepth - Depth))
        Depth += RootDepth[i];
      else
        Depth = TotalDepth;

      if (Depth > *TableDepth) {
        if ((Depth - *TableDepth) > RootDepth[i])
          AvaWater = (Porosity[i] - FCap[i]) * RootDepth[i] * Adjust[i];
        else
          AvaWater = (Porosity[i] - FCap[i]) * (Depth - *TableDepth) * Adjust[i];
      }

      ExtracWater = (-SatFlow > AvaWater) ? -AvaWater : SatFlow;

      Moist[i] += ExtracWater / (RootDepth[i] * Adjust[i]);

      SatFlow -= ExtracWater;
      if (SatFlow == 0.0)
        break;
    }

    if (SatFlow<0.0) {
      DeepAvaWater = 0.0;
      DeepExtracWater = 0.0;
      if (Depth < TotalDepth) {
        Depth = TotalDepth;

        if ((Depth - *TableDepth) > DeepLayerDepth)
          DeepAvaWater = (DeepPorosity - DeepFCap) * DeepLayerDepth * Adjust[NSoilLayers];
        else
          DeepAvaWater = (DeepPorosity - DeepFCap) * (Depth - *TableDepth) * Adjust[NSoilLayers];
      }

      DeepExtracWater = (-SatFlow > DeepAvaWater) ? -DeepAvaWater : SatFlow;
      Moist[NSoilLayers] += DeepExtracWater / (DeepLayerDepth * Adjust[NSoilLayers]);
      SatFlow -= DeepExtracWater;
    }
  }

  if (SatFlow > 0.0) {

    Depth += DeepLayerDepth;
    DeepWaterGap = 0.0;
    DeepExtracWater = 0.0;
    if (Depth > (TotalDepth - *TableDepth)); {
      DeepWaterGap = (DeepPorosity - Moist[NSoilLayers]) * DeepLayerDepth * Adjust[NSoilLayers];
      DeepExtracWater = (SatFlow > DeepWaterGap) ? DeepWaterGap : SatFlow;
      SatFlow -= DeepExtracWater;
      Moist[NSoilLayers] += DeepExtracWater / (DeepLayerDepth * Adjust[NSoilLayers]);
    }

    if (SatFlow > 0.0) {
      for (i = NSoilLayers - 1; i >= 0; i--) {
        WaterGap = 0.0;
        ExtracWater = 0.0;
        Depth += RootDepth[i];
        if (Depth > (TotalDepth - *TableDepth)); {
          WaterGap = (Porosity[i] - Moist[i]) * RootDepth[i] * Adjust[i];
          ExtracWater = (SatFlow > WaterGap) ? WaterGap : SatFlow;
          SatFlow -= ExtracWater;
          Moist[i] += ExtracWater / (RootDepth[i] * Adjust[i]);
        }
        if (SatFlow == 0.0)
          break;
      }
    }
  }

  if (SatFlow > 0.0)
    *Runoff += SatFlow;

  assert(SatFlow >= 0.0);

}