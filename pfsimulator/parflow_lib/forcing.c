#include "parflow.h"
#include "hbt.h"
#include "string.h"

#include "forcing.h"


Forcing* ForcingInit(ProblemData *problem_data, Grid *metgrid, const char* name) {

  Forcing* forcing = ctalloc(Forcing, 1);

  char key[IDB_MAX_KEY_LEN];

  forcing->problem_data = problem_data;
  forcing->metgrid = metgrid;
  forcing->kind = FORCING_STATIONS;
  forcing->metgrid = metgrid;

  forcing->forcingstations->dem_vector = 
    NewVectorType(metgrid, 1, 1, vector_met);
  ReadPFBinary(forcing->forcingstations->dem_filename, 
               forcing->forcingstations->dem_vector);

  forcing->forcingstations->indicator_vector = 
    NewVectorType(metgrid, 1, 1, vector_met);
  ReadPFBinary(forcing->forcingstations->indicator_filename, 
               forcing->forcingstations->indicator_vector);

  forcing->forcingstations->iodb = 
    HBT_new(ForcingIODBCompare, NULL, NULL, NULL, 0);

  ForcingStationsInit(forcing);
  OpenStationFiles(forcing);

  return forcing;
}

void ForcingFree(Forcing* forcing) {
  //FreeVector(forcing->forcingstations->indicator_values);
  //FreeVector(forcing->forcingstations->dem_vector)
  // TODO: ...
}

// void Forcing1DInit() {}
// void Forcing2DInit() {}
// void Forcing3DInit() {}
// void ForcingNCInit() {}

void ForcingStationsInit(Forcing* forcing) {
  forcing->forcingstations = ctalloc(ForcingStations, 1);
  char* station_names = GetString("Solver.CLM.Stations.Names");
  NameArray station_names_na = NA_NewNameArray(station_names);
  int station_count = NA_Sizeof(station_names_na);
  forcing->forcingstations->count = station_count;
  forcing->forcingstations->stations = ctalloc(Station*, station_count);

  for (int i = 0; i < station_count; i++) {
    char* station_name = station_names_na->names[i];
    forcing->forcingstations->stations[i] = StationInit(station_name);

    Forcing_IODB_Entry *db_entry = ctalloc(Forcing_IODB_Entry, 1);
    db_entry->key = forcing->forcingstations->stations[i]->id;
    db_entry->value = forcing->forcingstations->stations[i];
    HBT_insert(forcing->forcingstations->iodb, db_entry, 0);
  }

  MarkStationsInSubgrid(forcing);
  OpenStationFiles(forcing);

  forcing->forcingstations->sw_vect = NewVectorType(forcing->metgrid, 1, 1, vector_met);
  InitVectorAll(forcing->forcingstations->sw_vect, 100.0);

  forcing->forcingstations->lw_vect = NewVectorType(forcing->metgrid, 1, 1, vector_met);
  InitVectorAll(forcing->forcingstations->lw_vect, 100.0);

  forcing->forcingstations->u_vect = NewVectorType(forcing->metgrid, 1, 1, vector_met);
  InitVectorAll(forcing->forcingstations->u_vect, 100.0);

  forcing->forcingstations->v_vect = NewVectorType(forcing->metgrid, 1, 1, vector_met);
  InitVectorAll(forcing->forcingstations->v_vect, 100.0);

  forcing->forcingstations->temp_vect = NewVectorType(forcing->metgrid, 1, 1, vector_met);
  InitVectorAll(forcing->forcingstations->temp_vect, 100.0);

  forcing->forcingstations->prcp_vect = NewVectorType(forcing->metgrid, 1, 1, vector_met);
  InitVectorAll(forcing->forcingstations->prcp_vect, 100.0);

  forcing->forcingstations->patm_vect = NewVectorType(forcing->metgrid, 1, 1, vector_met);
  InitVectorAll(forcing->forcingstations->patm_vect, 100.0);

  forcing->forcingstations->qatm_vect = NewVectorType(forcing->metgrid, 1, 1, vector_met);
  InitVectorAll(forcing->forcingstations->qatm_vect, 100.0);
}

Interpolation* InterpolationInit(char* station_name, char* param) {
  char key[IDB_MAX_KEY_LEN];

  Interpolation* interp = (Interpolation*) malloc(sizeof(Interpolation));

  NameArray interp_switch = NA_NewNameArray(INTERPOLATION_KIND_NAMES);
  sprintf(key, "Solver.CLM.Stations.%s.Interpolation.%s.Type", station_name, param);
  char* interp_kind_str = GetString(key);
  interp->kind = NA_NameToIndex(interp_switch, interp_kind_str);

  switch (interp->kind) {
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
      interp->factors->filename = GetString(key);
      ReadPFBinary(interp->factors->filename, 
                   interp->factors->factors_vector);
      break;
    default:
      InputError("Error: invalid value <%s> for key <%s>\n",
                  interp_kind_str, key);
  }

  return interp;
}

// TODO: modify so only stations in subgrid are allocated
Station* StationInit(char* name) {
  char key[IDB_MAX_KEY_LEN];

  Station* station = ctalloc(Station, 1);
  station->name = name;

  sprintf(key, "Solver.CLM.Stations.%s.Index", name);
  station->id = GetInt(key);

  sprintf(key, "Solver.CLM.Stations.%s.Elevation", name);
  station->elevation = GetDouble(key);

  sprintf(key, "Solver.CLM.Stations.%s.File", name);
  station->infile = GetString(key);

  station->sw_interp = InterpolationInit(name, "DSWR");
  station->lw_interp = InterpolationInit(name, "DLWR");
  station->prcp_interp = InterpolationInit(name, "APCP");
  station->temp_interp = InterpolationInit(name, "Temp");
  station->u_interp = InterpolationInit(name, "UGRD");
  station->v_interp = InterpolationInit(name, "VGRD");
  station->patm_interp = InterpolationInit(name, "Press");
  station->qatm_interp = InterpolationInit(name, "SPFH");
  station->fp = NULL;
  station->in_subgrid = false;

  return station;
}

void MarkStationsInSubgrid(Forcing* forcing) {
  SubgridArray *subgrids = GridSubgrids(forcing->metgrid); // TODO: POSSIBLE ERROR
  GrGeomSolid *gr_domain = forcing->problem_data->gr_domain;

  int is;
  ForSubgridI(is, subgrids)
  {
    Subgrid* subgrid = SubgridArraySubgrid(subgrids, is);
    Subvector* indi_subvector = VectorSubvector(forcing->forcingstations->indicator_vector, is);

    int ix = SubgridIX(subgrid);
    int iy = SubgridIY(subgrid);
    int iz = SubgridIZ(subgrid);

    int nx = SubgridNX(subgrid);
    int ny = SubgridNY(subgrid);
    int nz = SubgridNZ(subgrid);

    int r = SubgridRX(subgrid);

    double* indi_data = SubvectorData(indi_subvector);

    int i, j, k;
    GrGeomInLoop(i, j, k, gr_domain, r, ix, iy, iz, nx, ny, nz,
    {
      int ips = SubvectorEltIndex(indi_subvector, i, j, 0);
      int indi_index = (int) round(indi_data[ips]);
      Station* station = IODBGetStation(forcing, indi_index, true);
      station->in_subgrid = true;
    });
  }
}

int ForcingIODBCompare(void *a_obj, void *b_obj) {
  Forcing_IODB_Entry *a = (Forcing_IODB_Entry*) a_obj;
  Forcing_IODB_Entry *b = (Forcing_IODB_Entry*) b_obj;

  int v1 = a->key;
  int v2 = a->key;

  if (v1 < v2) {
    return -1;
  } else if (v1 > v2) {
    return 1;
  } else {
    return 0;
  }
}

Station* IODBGetStation(Forcing* forcing, int id, bool result_required) {
  
  Forcing_IODB_Entry* lookup_entry;
  Forcing_IODB_Entry* result;

  lookup_entry->key = (int) id;

  result = (Forcing_IODB_Entry*) 
    HBT_lookup(forcing->forcingstations->iodb, &lookup_entry);

  if (result)
  {
    return result->value;
  }
  else
  {
    if (result_required) {
      char key_str[128];
      sprintf(key_str, "%d", lookup_entry->key);
      InputError("Error: Unable to find station with index <%s> "
                "in station indicator file\n", key_str, "");
    } else {
      return NULL;
    }
  }
}


void ReadNextMeteoRecord(Forcing* forcing) {
  for (int i = 0; i < forcing->forcingstations->count; i++) {
    Station *station = forcing->forcingstations->stations[i];
    if (!station->in_subgrid) {
      continue;
    }
    MeteoRecord *r = station->current_record;
    //double c1, c2, c3, c4, c5, c6, c7, c8;
    int scanned = 
      fscanf(station->fp, "%lf%lf%lf%lf%lf%lf%lf%lf", 
        &(r->sw), &(r->lw), &(r->prcp), &(r->temp), &(r->u), &(r->v), &(r->patm), &(r->qatm));
      if (scanned != 8) {
        amps_Printf("Error reading entry from file: %s", station->infile);
        exit(1);
      }
      // r->sw = c1;
      // r->lw = c2;
      // r->prcp = c3;
      // r->temp = c4;
      // r->u = c5;
      // r->v = c6;
      // r->patm = c7;
      // r->qatm = c8;
    //station->current_record = r; // TODO: Probably unnecessary. Delete.
  }
}

void OpenStationFiles(Forcing* forcing) {
  for (int i = 0; i < (forcing->forcingstations->count); i++) {
    Station* station = forcing->forcingstations->stations[i];
    if (!station->in_subgrid) {
      continue;
    }
    station->fp = fopen(station->infile, "r");
    if (station->fp == NULL) {
      printf("Failed to open input file %s for reading.", station->infile);
      exit(1);
    }

    station->current_record = ctalloc(MeteoRecord, 1);
  }
}

void ForcingGetCLMInputs(Forcing* forcing, double* sw, double* lw, double* u, double* v, 
  double* temp, double* prcp, double* patm, double* qatm) {

  SubgridArray *subgrids = GridSubgrids(forcing->metgrid); // TODO: POSSIBLE ERROR
  GrGeomSolid *gr_domain = forcing->problem_data->gr_domain;

  int is;
  ForSubgridI(is, subgrids)
  {
    Subgrid* subgrid = SubgridArraySubgrid(subgrids, is);

    int ix = SubgridIX(subgrid);
    int iy = SubgridIY(subgrid);
    int iz = SubgridIZ(subgrid);

    int nx = SubgridNX(subgrid);
    int ny = SubgridNY(subgrid);
    int nz = SubgridNZ(subgrid);

    int r = SubgridRX(subgrid);

    Subvector* indi_subvector = VectorSubvector(forcing->forcingstations->indicator_vector, is);
    Subvector* sw_subvector = VectorSubvector(forcing->forcingstations->sw_vect, is);
    Subvector* lw_subvector = VectorSubvector(forcing->forcingstations->lw_vect, is);
    Subvector* u_subvector = VectorSubvector(forcing->forcingstations->u_vect, is);
    Subvector* v_subvector = VectorSubvector(forcing->forcingstations->v_vect, is);
    Subvector* temp_subvector = VectorSubvector(forcing->forcingstations->temp_vect, is);
    Subvector* prcp_subvector = VectorSubvector(forcing->forcingstations->prcp_vect, is);
    Subvector* patm_subvector = VectorSubvector(forcing->forcingstations->patm_vect, is);
    Subvector* qatm_subvector = VectorSubvector(forcing->forcingstations->qatm_vect, is);

    double* indi_data = SubvectorData(indi_subvector);
    double* sw_data = SubvectorData(sw_subvector);
    double* lw_data = SubvectorData(lw_subvector);
    double* u_data = SubvectorData(u_subvector);
    double* v_data = SubvectorData(v_subvector);
    double* temp_data = SubvectorData(temp_subvector);
    double* prcp_data = SubvectorData(prcp_subvector);
    double* patm_data = SubvectorData(patm_subvector);
    double* qatm_data = SubvectorData(qatm_subvector);

    int i, j, k;
    GrGeomInLoop(i, j, k, gr_domain, r, ix, iy, iz, nx, ny, nz,
    {
      int ips = SubvectorEltIndex(indi_subvector, i, j, 0);
      int indi_index = (int) round(indi_data[ips]);
      Station* station = IODBGetStation(forcing, indi_index, true);
      MeteoRecord *r = station->current_record;
      sw_data[ips] = r->sw; // TODO: is ips correct? Sw other method in solver_richards.c (for (n = 0; n < ((nx + 2) * (ny + 2) * 3); n++))
      lw_data[ips] = r->lw;
      u_data[ips] = r->u;
      v_data[ips] = r->v;
      temp_data[ips] = r->temp;
      prcp_data[ips] = r->prcp;
      patm_data[ips] = r->patm;
      qatm_data[ips] = r->qatm;
    });
  } 

}