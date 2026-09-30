#include "propagator.h"
#include "astro_constants.h"

#include <math.h>
#include <string.h>

// global setting mirrored from AppConfig
static bool g_use_short_period = true;

void sat_prop_set_short_period(bool enabled) { g_use_short_period = enabled; }

// copy the reusable SGP4 coefficients + epoch mean elements into prop
static void sat_prop_capture(Satellite *sat)
{
    struct elsetrec *rec = &sat->satrec;
    SatPropState *p = &sat->prop;

    p->mo = rec->mm;
    p->argpo = rec->om;
    p->nodeo = rec->Om;
    p->inclo = rec->im;
    p->ecco = rec->em;
    p->no_unkozai = rec->nm;

    p->mdot = rec->mdot;
    p->argpdot = rec->argpdot;
    p->nodedot = rec->nodedot;
    p->nodecf = rec->nodecf;

    p->bstar = rec->bstar;
    p->cc1 = rec->cc1;
    p->cc4 = rec->cc4;
    p->cc5 = rec->cc5;
    p->t2cof = rec->t2cof;
    p->omgcof = rec->omgcof;
    p->xmcof = rec->xmcof;
    p->eta = rec->eta;
    p->d2 = rec->d2;
    p->d3 = rec->d3;
    p->d4 = rec->d4;
    p->t3cof = rec->t3cof;
    p->t4cof = rec->t4cof;
    p->t5cof = rec->t5cof;
    p->isimp = rec->isimp;

    p->radiusearthkm = rec->radiusearthkm;
    p->xke = rec->xke;
    p->j2 = rec->j2;
    p->j3oj2 = rec->j3oj2;
    p->con41 = rec->con41;
    p->x1mth2 = rec->x1mth2;
    p->x7thm1 = rec->x7thm1;
    p->aycof = rec->aycof;
    p->xlcof = rec->xlcof;

    // drag terms are anchored to the reference mean anomaly
    double delmotemp = 1.0 + p->eta * cos(p->mo);
    p->delmo = delmotemp * delmotemp * delmotemp;
    p->sinmao = sin(p->mo);

    p->is_deep_space = (rec->method == 'd');
    p->prop_failed = (rec->error != 0);
}

bool sat_prop_init(Satellite *sat)
{
    memset(&sat->prop, 0, sizeof(SatPropState));

    double no_kozai = sat->mean_motion * 60.0;
    int ret = sgp4init_from_elements(
        &sat->satrec,
        sat->epoch_unix,
        sat->bstar,
        0.0,
        0.0,
        sat->eccentricity,
        sat->arg_perigee,
        sat->inclination,
        sat->mean_anomaly,
        no_kozai,
        sat->raan);

    if (ret != 0 || sat->satrec.error != 0)
    {
        sat->prop.valid = false;
        return false;
    }

    sat_prop_capture(sat);
    sat->prop.valid = true;
    return true;
}

// SGP4 near-earth secular + drag + Kepler + J2 short-period (mirrors csgp4 sgp4())
static void prop_compute(const SatPropState *p, double t, double r[3], double v[3], SatPropElements *elem)
{
    const double twopi = 2.0 * SGPPI; // double pi, matching sgp4()'s twopi (raylib PI is float)
    double t2 = t * t;

    double xmdf = p->mo + p->mdot * t;
    double argpdf = p->argpo + p->argpdot * t;
    double nodedf = p->nodeo + p->nodedot * t;
    double argpm = argpdf;
    double mm = xmdf;
    double nodem = nodedf + p->nodecf * t2;
    double tempa = 1.0 - p->cc1 * t;
    double tempe = p->bstar * p->cc4 * t;
    double templ = p->t2cof * t2;

    if (p->isimp != 1)
    {
        double delomg = p->omgcof * t;
        double delmtemp = 1.0 + p->eta * cos(xmdf);
        double delm = p->xmcof * (delmtemp * delmtemp * delmtemp - p->delmo);
        double temp = delomg + delm;
        mm = xmdf + temp;
        argpm = argpdf - temp;
        double t3 = t2 * t;
        double t4 = t3 * t;
        tempa -= p->d2 * t2 + p->d3 * t3 + p->d4 * t4;
        tempe += p->bstar * p->cc5 * (sin(mm) - p->sinmao);
        templ += p->t3cof * t3 + t4 * (p->t4cof + t * p->t5cof);
    }

    double nm = p->no_unkozai;
    double em = p->ecco;
    double inclm = p->inclo;

    double am = pow(p->xke / nm, 2.0 / 3.0) * tempa * tempa;
    nm = p->xke / pow(am, 1.5);
    em = em - tempe;
    if (em < 1.0e-6)
        em = 1.0e-6;
    mm = mm + p->no_unkozai * templ;
    double xlm = mm + argpm + nodem;
    nodem = fmod(nodem, twopi);
    argpm = fmod(argpm, twopi);
    xlm = fmod(xlm, twopi);
    mm = fmod(xlm - argpm - nodem, twopi);

    if (elem)
    {
        elem->sma_km = am * p->radiusearthkm;
        elem->ecc = em;
        elem->mean_motion = nm;
        elem->incl = inclm;
        elem->raan = nodem;
        elem->argp = argpm;
        elem->mean_anom = mm;
        elem->valid = true;
    }

    double axnl = em * cos(argpm);
    double temp = 1.0 / (am * (1.0 - em * em));
    double aynl = em * sin(argpm) + temp * p->aycof;
    double xl = mm + argpm + nodem + temp * p->xlcof * axnl;

    double u = fmod(xl - nodem, twopi);
    double E = u;
    double tem5 = 9999.9;
    int ktr = 1;
    double coseo1 = 0.0, sineo1 = 0.0;
    while ((fabs(tem5) >= 1.0e-12) && (ktr <= 10))
    {
        sineo1 = sin(E);
        coseo1 = cos(E);
        tem5 = 1.0 - coseo1 * axnl - sineo1 * aynl;
        tem5 = (u - aynl * coseo1 + axnl * sineo1 - E) / tem5;
        if (fabs(tem5) >= 0.95)
            tem5 = (tem5 > 0.0) ? 0.95 : -0.95;
        E = E + tem5;
        ktr = ktr + 1;
    }

    double ecose = axnl * coseo1 + aynl * sineo1;
    double esine = axnl * sineo1 - aynl * coseo1;
    double el2 = axnl * axnl + aynl * aynl;
    double pl = am * (1.0 - el2);
    if (pl < 0.0)
    {
        r[0] = r[1] = r[2] = NAN;
        v[0] = v[1] = v[2] = NAN;
        return;
    }

    double rl = am * (1.0 - ecose);
    double rdotl = sqrt(am) * esine / rl;
    double rvdotl = sqrt(pl) / rl;
    double betal = sqrt(1.0 - el2);
    temp = esine / (1.0 + betal);
    double sinu = am / rl * (sineo1 - aynl - axnl * temp);
    double cosu = am / rl * (coseo1 - axnl + aynl * temp);
    double su = atan2(sinu, cosu);
    double sin2u = 2.0 * cosu * sinu;
    double cos2u = 1.0 - 2.0 * sinu * sinu;
    temp = 1.0 / pl;
    double temp1 = 0.5 * p->j2 * temp;
    double temp2 = temp1 * temp;

    double mrt, mvt, rvdot, xnode, xinc;
    if (g_use_short_period)
    {
        mrt = rl * (1.0 - 1.5 * temp2 * betal * p->con41) + 0.5 * temp1 * p->x1mth2 * cos2u;
        su = su - 0.25 * temp2 * p->x7thm1 * sin2u;
        xnode = nodem + 1.5 * temp2 * cos(inclm) * sin2u;
        xinc = inclm + 1.5 * temp2 * cos(inclm) * sin(inclm) * cos2u;
        mvt = rdotl - nm * temp1 * p->x1mth2 * sin2u / p->xke;
        rvdot = rvdotl + nm * temp1 * (p->x1mth2 * cos2u + 1.5 * p->con41) / p->xke;
    }
    else
    {
        mrt = rl;
        xnode = nodem;
        xinc = inclm;
        mvt = rdotl;
        rvdot = rvdotl;
    }

    double sinsu = sin(su), cossu = cos(su);
    double snod = sin(xnode), cnod = cos(xnode);
    double sini = sin(xinc), cosi = cos(xinc);
    double xmx = -snod * cosi;
    double xmy = cnod * cosi;
    double ux = xmx * sinsu + cnod * cossu;
    double uy = xmy * sinsu + snod * cossu;
    double uz = sini * sinsu;
    double vx = xmx * cossu - cnod * sinsu;
    double vy = xmy * cossu - snod * sinsu;
    double vz = sini * cossu;

    double vkmpersec = p->radiusearthkm * p->xke / 60.0;
    r[0] = mrt * ux * p->radiusearthkm;
    r[1] = mrt * uy * p->radiusearthkm;
    r[2] = mrt * uz * p->radiusearthkm;
    v[0] = (mvt * ux + rvdot * vx) * vkmpersec;
    v[1] = (mvt * uy + rvdot * vy) * vkmpersec;
    v[2] = (mvt * uz + rvdot * vz) * vkmpersec;
}

void sat_prop_state(const Satellite *sat, double unix, Vector3 *out_pos, Vector3 *out_vel)
{
    const SatPropState *p = &sat->prop;
    double ro[3] = {0}, vo[3] = {0};

    if (!p->valid || p->prop_failed)
    {
        if (out_pos) *out_pos = (Vector3){NAN, NAN, NAN};
        if (out_vel) *out_vel = (Vector3){NAN, NAN, NAN};
        return;
    }

    if (p->is_deep_space)
    {
        // deep-space needs resonance + lunar-solar periodics: full SGP4
        struct elsetrec *rec = (struct elsetrec *)&sat->satrec;
        double tsince = (unix - sat->epoch_unix) / 60.0;
        sgp4(rec, tsince, ro, vo);
        if (rec->error != 0)
        {
            if (out_pos) *out_pos = (Vector3){NAN, NAN, NAN};
            if (out_vel) *out_vel = (Vector3){NAN, NAN, NAN};
            return;
        }
    }
    else
    {
        double t = (unix - sat->epoch_unix) / 60.0;
        prop_compute(p, t, ro, vo, NULL);
    }

    // app axis convention: TEME (x, y, z) -> (x, z, -y)
    if (out_pos) *out_pos = (Vector3){(float)ro[0], (float)ro[2], (float)(-ro[1])};
    if (out_vel) *out_vel = (Vector3){(float)vo[0], (float)vo[2], (float)(-vo[1])};
}

// fall back to the epoch elements (converted to SatPropElements units)
static void sat_prop_epoch_elements(const Satellite *sat, SatPropElements *out)
{
    out->sma_km = sat->semi_major_axis;
    out->ecc = sat->eccentricity;
    out->mean_motion = sat->mean_motion * 60.0;
    out->incl = sat->inclination;
    out->raan = sat->raan;
    out->argp = sat->arg_perigee;
    out->mean_anom = sat->mean_anomaly;
    out->valid = false;
}

void sat_prop_elements(const Satellite *sat, double unix, SatPropElements *out)
{
    memset(out, 0, sizeof(*out));
    const SatPropState *p = &sat->prop;

    if (!p->valid || p->prop_failed)
    {
        sat_prop_epoch_elements(sat, out);
        return;
    }

    if (p->is_deep_space)
    {
        // deep-space mean elements come from the same full-SGP4 call as the position path
        struct elsetrec *rec = (struct elsetrec *)&sat->satrec;
        double ro[3] = {0}, vo[3] = {0};
        sgp4(rec, (unix - sat->epoch_unix) / 60.0, ro, vo);
        if (rec->error != 0)
        {
            sat_prop_epoch_elements(sat, out);
            return;
        }
        out->sma_km = rec->am * rec->radiusearthkm;
        out->ecc = rec->em;
        out->mean_motion = rec->nm;
        out->incl = rec->im;
        out->raan = rec->Om;
        out->argp = rec->om;
        out->mean_anom = rec->mm;
    }
    else
    {
        double ro[3], vo[3];
        prop_compute(p, (unix - sat->epoch_unix) / 60.0, ro, vo, out);
    }
    out->valid = true;
}

Vector3 sat_prop_position(const Satellite *sat, double unix)
{
    Vector3 pos;
    sat_prop_state(sat, unix, &pos, NULL);
    return pos;
}

Vector3 sat_prop_velocity(const Satellite *sat, double unix)
{
    Vector3 vel;
    sat_prop_state(sat, unix, NULL, &vel);
    return vel;
}
