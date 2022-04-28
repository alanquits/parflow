#include "parflow.h"
#include "hbt.h"
#include "string.h"

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
  double rate;          // lapse rate (e.g., degrees C / m elevation change)
} LapseInterpolation;

typedef struct {
  char* filename;
  Vector* factors_vector;      // TODO: grid of multipliers (might be useful for applying shaded areas)
} FactorInterpolation;

typedef struct {
  InterpolationKind kind;
  union {
    LapseInterpolation lapse;
    FactorInterpolation factors
  }
} Interpolation;

// typedef struct {} Forcing1D;
// typedef struct {} Forcing2D;
// typedef struct {} Forcing3D;
// typedef struct {} ForcingNC;
typedef struct {
  Station* stations; 
  int count;                  // Number of stations in model
  Station* stations_sub;      // Only stations in subgrid
  int stations_sub_count;     // Number of stations in subgrid     
  char  *indicator_filename;
  Vector *indicator_vector;
  char  *dem_filename;
  Vector *dem_vector;
} ForcingStations;

typedef struct {
  int id;                     // Indicator id
  char* name;                 // Name of station
  double elevation;           // Elevation
  char* infile;               // Full path to data file
  FILE* fp;                   // File pointer (NULL if station is unused)
  bool read_required;         // True when time to read another line from fp
  Interpolation* sw_interp;   // 
  Interpolation* lw_interp;   // 
  Interpolation* u_interp;    //
  Interpolation* v_interp;    // 
  Interpolation* temp_interp; // 
  Interpolation* prcp_interp; // Precipitation interpolation
  Interpolation* patm_interp; //
  Interpolation* qatm_interp; //
  MeteoRecord current_record  // Most recent record read 
} Station;

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
} PublicXtra;

typedef struct {
  Grid *metgrid; 
  char *metfile;
  char *metpath;
  bool *metsub;
  void *dem;     // TODO: fix datatype
  ForcingKind kind;
  union {
      // Forcing1D* forcing1d;
      // Forcing2D* forcing2d;
      // Forcing3D* forcing3d;
      // ForcingNC* forcingnc;
      ForcingStations* forcingstations; 
  };
} InstanceXtra;

void Forcing(const char* name) {
  PFModule* this_module = ThisPFModule;
  PublicXtra* public_xtra = PFModulePublicXtra(this_module);
  InstanceXtra* instance_xtra = PFModuleInstanceXtra(this_module);
}

PFModule* ForcingInitInstanceXtra(const char* name, Grid *metgrid) {
  PFModule      *this_module   = ThisPFModule;
  PublicXtra    *public_xtra   = PFModulePublicXtra(this_module);
  InstanceXtra  *instance_xtra;

  if ( PFModuleInstanceXtra(this_module) == NULL ) {
    instance_xtra = ctalloc(InstanceXtra, 1);
  } else {
    instance_xtra = PFModuleInstanceXtra(this_module);
  }

  char key[IDB_MAX_KEY_LEN];

  instance_xtra->metgrid = metgrid;
  instance_xtra->kind = FORCING_STATIONS;
  instance_xtra->metgrid = metgrid;
  ForcingStationsInit();

  ReadPFBinary(instance_xtra->forcingstations->dem_filename, 
               instance_xtra->forcingstations->dem_vector);

  ReadPFBinary(instance_xtra->forcingstations->indicator_filename, 
               instance_xtra->forcingstations->indicator_vector);
}

void ForcingFreeInstanceXtra() {
  FreeVector(instance_xtra->forcingstations->indicator_values);
  FreeVector(instance_xtra->forcingstations->dem_vector)
  // TODO: ...
}

PFModule* ForcingNewPublicXtra(int arg1, int argc2) {
}

void ForcingFreePublicXtra() {
}

int ForcingSizeOfTempData() {}

// void Forcing1DInit() {}
// void Forcing2DInit() {}
// void Forcing3DInit() {}
// void ForcingNCInit() {}

void ForcingStationsInit() {
  PublicXtra *public_xtra = (PublicXtra*)PFModulePublicXtra(this_module);
  InstanceXtra *instance_xtra =
    (InstanceXtra*)PFModuleInstanceXtra(this_module);

  char* station_names = GetString("Solver.CLM.Stations.Names");
  NameArray station_names_na = NA_NewNameArray(station_names);
  int station_count = NA_Sizeof(station_names_na);
  instance_xtra->forcingstations->count = station_count;
  instance_xtra->forcingstations->stations = ctalloc(Station, station_count);

  for (int i = 0; i < station_count; i++) {
    char* station_name = station_names_na->names[i];
    instance_xtra->forcingstations->stations[i] = StationInit(name);
  }
}

Interpolation* InterpolationInit(const char* station_name, const char* param) {
  char key[IDB_MAX_KEY_LEN];

  Interpolation* interp = (Interpolation*) malloc(sizeof(Interpolation));

  NameArray interp_switch = NA_NewNameArray(INTERPOLATION_KIND_NAMES);
  sprintf(key, "Solver.CLM.Stations.%s.Interpolation.%s.Type", station_name, param);
  char* interp_kind_str = GetString(key);
  interp->kind = NA_NameToIndex(interp_switch, interp_kind_str);

  switch interp->kind {
    case INTERPOLATION_NONE:
      break;
    case INTERPOLATION_LAPSE:
      interp->lapse = ctalloc(LapseInterpolation, 1);
      sprintf(key, "Solver.CLM.Stations.%s.Interpolation.%s.Lapse", 
      station_name, param);
      interp->lapse->rate = GetDouble(key);
      break;
    case INTERPOLATION_FACTOR:
      interp->factors = ctalloc(FactorInterpolation, 1);
      sprintf(key, "Solver.CLM.Stations.%s.Interpolation.%s.Factors.File", 
        station_name, param);
      interp->factors->infile = GetString(key);
      ReadPFBinary(interp->factors_filename, 
                   interp->factors_vector);
      break;
    default:
      InputError("Error: invalid value <%s> for key <%s>\n",
                  interp_kind_str, key);
  }

  return interp;
}

Station* StationInit(const char* name) {
  char key[IDB_MAX_KEY_LEN];
    
  Station* station = ctalloc(Station, 1);
  station->name = name;

  sprintf(key, "Solver.CLM.Stations.%s.Index", station_name);
  station->id = GetInt(key);

  sprintf(key, "Solver.CLM.Stations.%s.Elevation", station_name);
  station->elevation = GetDouble(key);

  sprintf(key, "Solver.CLM.Stations.%s.File", station_name);
  station->infile = GetString(key);

  // File reading is performed on rank 0 and distributed to other processes.
  if (amps_Rank(amps_CommWorld) == 0) {
    if ( (station->fp = fopen(station->infile, "r") ) == NULL ) {
      amps_Printf("Failed to open file '%s' for reading.")
    } else {
      station->fp = NULL;
    }
  }
  
  station->sw_interp = InitInterpolation(name, "DSWR");
  station->lw_interp = InitInterpolation(name, "DLWR");
  station->prcp_interp = InitInterpolation(name, "APCP");
  station->temp_interp = InitInterpolation(name, "Temp");
  station->u_interp = InitInterpolation(name, "UGRD");
  station->v_interp = InitInterpolation(name, "VGRD");
  station->patm_interp = InitInterpolation(name, "Press");
  station->qatm_interp = InitInterpolation(name, "SPFH");

  station->read_required = true;

  return station;
}
