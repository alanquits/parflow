#ifndef FORCING_H
#define FORCING_H

#include "parflow.h"
#include <stdio.h>

#define MAX_FILENAME_LEN 2048

// Eventually, other forcing methods can be migrated from solver_richards.c
// into this module...
#define FORCING_KIND_NAMES "none 1D 2D 3D NC Stations"
typedef enum {
    FORCING_NONE = 0,
    FORCING_1D,
    FORCING_2D,
    FORCING_3D,
    FORCING_NC,
    FORCING_STATIONS,
} ForcingKind;

#define INTERPOLATION_KIND_NAMES "None Linear Factor"
typedef enum {
  INTERPOLATION_NONE = 0,
  INTERPOLATION_LAPSE,
  INTERPOLATION_FACTOR
} InterpolationKind;

typedef struct {
  double rate;            // lapse rate (e.g., degrees C / m elevation change)
} LapseInterpolation;

typedef struct {
  char* filename;
  Vector* factors_vector; // grid of multipliers (might be useful for applying shaded areas)
} FactorInterpolation;

typedef struct {
  InterpolationKind kind;
  union {
    LapseInterpolation *lapse;
    FactorInterpolation *factors;
  };
} Interpolation;

typedef struct {
  double sw;
  double lw;
  double u;
  double v;
  double temp;
  double prcp;
  double patm;
  double qatm;
} MeteoRecord;

typedef struct {
  int id;                     // Indicator id
  char* name;                 // Name of station
  double elevation;           // Elevation
  char* infile;               // Full path to data file
  FILE* fp;                   // File pointer (NULL if station is unused)
  bool in_subgrid;            // True if station is in current subgrid
  Interpolation* sw_interp;   // 
  Interpolation* lw_interp;   // 
  Interpolation* u_interp;    //
  Interpolation* v_interp;    // 
  Interpolation* temp_interp; // 
  Interpolation* prcp_interp; // Precipitation interpolation
  Interpolation* patm_interp; //
  Interpolation* qatm_interp; //
  MeteoRecord *current_record;  // Most recent record read 
} Station;

// typedef struct {} Forcing1D;
// typedef struct {} Forcing2D;
// typedef struct {} Forcing3D;
// typedef struct {} ForcingNC;
typedef struct {
  Station** stations; 
  int count;                  // Number of stations in model
  Station** stations_sub;      // Only stations in subgrid
  int stations_sub_count;     // Number of stations in subgrid     
  char  *indicator_filename;
  Vector *indicator_vector;
  char  *dem_filename;
  Vector *dem_vector;
  int *id_to_index;
  HBT* iodb;
  Vector* sw_vect;
  Vector* lw_vect;
  Vector* u_vect;
  Vector* v_vect;
  Vector* temp_vect;
  Vector* prcp_vect;
  Vector* patm_vect;
  Vector* qatm_vect;
} ForcingStations;

typedef struct {
  int key;
  Station* value;
} Forcing_IODB_Entry;

typedef struct {
  Grid *metgrid; 
  char *metfile;
  char *metpath;
  bool *metsub;
  void *dem;     // TODO: fix datatype
  ForcingKind kind;
  ProblemData *problem_data;
  union {
      // Forcing1D* forcing1d;
      // Forcing2D* forcing2d;
      // Forcing3D* forcing3d;
      // ForcingNC* forcingnc;
      ForcingStations* forcingstations; 
  };
} Forcing;

Forcing* ForcingInit(ProblemData *problem_data, Grid *metgrid, const char* name);
void ForcingStationsInit(Forcing* forcing);
void MarkStationsInSubgrid(Forcing* forcing);
Station* StationInit(char* name);
Interpolation* InterpolationInit(char* station_name, char* param);
int ForcingIODBCompare(void *a_obj, void *b_obj);
Station* IODBGetStation(Forcing* forcing, int id, bool result_required);
void ReadNextMeteoRecord(Forcing* forcing);
void OpenStationFiles(Forcing* forcing);
void ForcingGetCLMInputs(Forcing* forcing, double* sw, double* lw, double* u, double* v, 
  double* temp, double* prcp, double* patm, double* qatm);

#endif