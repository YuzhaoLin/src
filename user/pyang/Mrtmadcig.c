/* RTM and angle gather (ADCIG) extraction using poynting vector
NB: SPML boundary condition combined with 4-th order finite difference,
effective boundary saving strategy used!
*/
/*
  Copyright (C) 2012  Xi'an Jiaotong University (Pengliang Yang)

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

  Reference: 
	[1] Yoon, Kwangjin, and Kurt J. Marfurt. "Reverse-time migration 
	using the Poynting vector." Exploration Geophysics 37.1 (2006): 
	102-107.
  	[2] Costa, J. C., et al. "Obliquity-correction imaging condition 
	for reverse time migration." Geophysics 74.3 (2009): S57-S66.
*/
#include <rsf.h>

#ifdef _OPENMP
#include <omp.h>
#endif

static bool 	csdgather; 	// common shot gather (CSD) or not 
static int 	nb, nz, nx, nzpad, nxpad, nt, ns, ng;
static float 	fm, dt, dz, dx, _dz, _dx, vmute;

void expand2d(float** b, float** a)
/*< expand domain of 'a' to 'b': source(a)-->destination(b) >*/
{
    int iz,ix;

#ifdef _OPENMP
#pragma omp parallel for default(none)	\
	private(ix,iz)			\
	shared(b,a,nb,nz,nx)
#endif
    for     (ix=0;ix<nx;ix++) {
	for (iz=0;iz<nz;iz++) {
	    b[nb+ix][nb+iz] = a[ix][iz];
	}
    }

    for     (ix=0; ix<nxpad; ix++) {
	for (iz=0; iz<nb;    iz++) {
	    b[ix][      iz  ] = b[ix][nb  ];
	    b[ix][nzpad-iz-1] = b[ix][nzpad-nb-1];
	}
    }

    for     (ix=0; ix<nb;    ix++) {
	for (iz=0; iz<nzpad; iz++) {
	    b[ix 	 ][iz] = b[nb  		][iz];
	    b[nxpad-ix-1 ][iz] = b[nxpad-nb-1	][iz];
	}
    }
}


void window2d(float **a, float **b)
/*< window 'b' to 'a': source(b)-->destination(a) >*/
{
    int iz,ix;

#ifdef _OPENMP
#pragma omp parallel for default(none)	\
	private(ix,iz)			\
	shared(b,a,nb,nz,nx)
#endif
    for     (ix=0;ix<nx;ix++) {
	for (iz=0;iz<nz;iz++) {
	    a[ix][iz]=b[nb+ix][nb+iz] ;
	}
    }
}

void  pmlcoeff_init(float *d1z, float *d2x, float vmax)
/*< initialize PML abosorbing coefficients >*/
{
	int ix, iz;
 	float Rc=1.e-5;
    	float x, z, L=nb* SF_MAX(dx,dz);
    	float d0=-3.*vmax*logf(Rc)/(2.*L*L*L);

	for(ix=0; ix<nxpad; ix++)
	{
		x=0;
	    	if (ix>=0 && ix<nb)   x=(ix-nb)*dx;
	    	else if(ix>=nxpad-nb && ix<nxpad) x=(ix-(nxpad-nb-1))*dx;
	    	d2x[ix] = d0*x*x;
	}
	for(iz=0; iz<nzpad; iz++)
	{
		z=0;
	    	if (iz>=0 && iz<nb)   z=(iz-nb)*dz;
	    	else if(iz>=nzpad-nb && iz<nzpad) z=(iz-(nzpad-nb-1))*dz; 
	    	d1z[iz] = d0*z*z;  
	}
}

void sg_init(int *sxz, int szbeg, int sxbeg, int jsz, int jsx, int ns)
/*< shot/geophone position initialize
sxz/gxz; szbeg/gzbeg; sxbeg/gxbeg; jsz/jgz; jsx/jgx; ns/ng; >*/
{
	int is, sz, sx;

	for(is=0; is<ns; is++)
	{
		sz=szbeg+is*jsz;
		sx=sxbeg+is*jsx;
		sxz[is]=sz+nz*sx;
	}
}

void wavefield_init(float **p, float **pz, float **px, float **vx, float **vz)
{
	memset(p[0],0,nzpad*nxpad*sizeof(float));
	memset(pz[0],0,nzpad*nxpad*sizeof(float));
	memset(px[0],0,nzpad*nxpad*sizeof(float));
	memset(vz[0],0,nzpad*nxpad*sizeof(float));
	memset(vx[0],0,nzpad*nxpad*sizeof(float));
}


void add_source(int *sxz, float **p, int ns, float *source, bool add)
/*< add seismic sources in grid >*/
{
	int is, sx, sz;

	if(add){// add sources
#ifdef _OPENMP
#pragma omp parallel for default(none)	\
	private(is,sx,sz)		\
	shared(p,source,sxz,nb,ns,nz)
#endif
		for(is=0;is<ns; is++){
			sx=sxz[is]/nz+nb;
			sz=sxz[is]%nz+nb;
			p[sx][sz]+=source[is];
		}
	}else{ // subtract sources
#ifdef _OPENMP
#pragma omp parallel for default(none)	\
	private(is,sx,sz)		\
	shared(p,source,sxz,nb,ns,nz)
#endif
		for(is=0;is<ns; is++){
			sx=sxz[is]/nz+nb;
			sz=sxz[is]%nz+nb;
			p[sx][sz]-=source[is];
		}
	}
}


void step_forward(float **p, float **pz, float **px, float **vz, float **vx, float **vv, float *d1z, float *d2x)
{
	int i1, i2;
	float tmp, diff1, diff2;

#ifdef _OPENMP
#pragma omp parallel for default(none)			\
	private(i1,i2,diff1,diff2)			\
	shared(nzpad, nxpad, p, vz, vx, dt, _dz, _dx, d1z, d2x)
#endif
	for(i2=3; i2<nxpad-4; i2++)
	for(i1=3; i1<nzpad-4; i1++)
	{
		diff1=1.125*(p[i2][i1+1]-p[i2][i1])-0.041666666666667*(p[i2][i1+2]-p[i2][i1-1]);
		diff2=1.125*(p[i2+1][i1]-p[i2][i1])-0.041666666666667*(p[i2+2][i1]-p[i2-1][i1]);
		vz[i2][i1]=((1.-0.5*dt*d1z[i1])*vz[i2][i1]+dt*_dz*diff1)/(1.+0.5*dt*d1z[i1]);
		vx[i2][i1]=((1.-0.5*dt*d2x[i2])*vx[i2][i1]+dt*_dx*diff2)/(1.+0.5*dt*d2x[i2]);
	}

#ifdef _OPENMP
#pragma omp parallel for default(none)			\
	private(i1,i2,diff1, diff2, tmp)		\
	shared(nzpad, nxpad, vv, p, pz, px, vz, vx, dt, _dz, _dx, d1z, d2x)
#endif
	for(i2=4; i2<nxpad-3; i2++)
	for(i1=4; i1<nzpad-3; i1++)
	{
		tmp=vv[i2][i1]; tmp=tmp*tmp;
		diff2=vx[i2][i1]-vx[i2-1][i1];
		diff1=vz[i2][i1]-vz[i2][i1-1];

		diff1=1.125*(vz[i2][i1]-vz[i2][i1-1])-0.041666666666667*(vz[i2][i1+1]-vz[i2][i1-2]);
		diff2=1.125*(vx[i2][i1]-vx[i2-1][i1])-0.041666666666667*(vx[i2+1][i1]-vx[i2-2][i1]);
		pz[i2][i1]=((1.-0.5*dt*d1z[i1])*pz[i2][i1]+dt*tmp*_dz*diff1)/(1.+0.5*dt*d1z[i1]);
		px[i2][i1]=((1.-0.5*dt*d2x[i2])*px[i2][i1]+dt*tmp*_dx*diff2)/(1.+0.5*dt*d2x[i2]);
		p[i2][i1]=px[i2][i1]+pz[i2][i1];
	}
}


void record_seis(float *seis_it, int *gxz, float **p, int ng)
/*< record seismogram at time it into a vector length of ng >*/
{
	int ig, gx, gz;

#ifdef _OPENMP
#pragma omp parallel for default(none)	\
	private(ig,gx,gz)		\
	shared(seis_it,p,gxz,nb,ng,nz)
#endif
	for(ig=0;ig<ng; ig++)
	{
		gx=gxz[ig]/nz+nb;
		gz=gxz[ig]%nz+nb;
		seis_it[ig]=p[gx][gz];
	}
}


void matrix_transpose(float **mat, int n1, int n2)
/*< transpose a matrix n1xn2 into n2xn1 >*/
{
	int i1, i2;
	float **tmp;
	tmp=sf_floatalloc2(n2, n1);

	for(i2=0; i2<n2; i2++)
	for(i1=0; i1<n1; i1++)
	{
		tmp[i1][i2]=mat[i2][i1];
	}
	memcpy(mat[0], tmp[0], n1*n2*sizeof(float));

	free(*tmp);free(tmp);
}



void cross_correlation(float **sp, float **gp, float **num, float **den)
/*< compute cross-correlation >*/
{
	int ix,iz;

#ifdef _OPENMP
#pragma omp parallel for default(none)	\
    private(ix,iz)			\
    shared(num,den,sp,gp,nx,nz,nb)  
#endif 
	for(ix=0; ix<nx; ix++)
	for(iz=0; iz<nz; iz++)
	{
		num[ix][iz]+=sp[ix+nb][iz+nb]*gp[ix+nb][iz+nb];
		den[ix][iz]+=sp[ix+nb][iz+nb]*sp[ix+nb][iz+nb];
	}
}

void normalize_image(float *imag1, float *imag2, float **num, float **den)
/*< do image normalization >*/
{
	int ix,iz;

#ifdef _OPENMP
#pragma omp parallel for default(none)	\
    private(ix,iz)			\
    shared(imag1,imag2,num, den, nx,nz)  
#endif 
	for(ix=0; ix<nx; ix++)
	for(iz=0; iz<nz; iz++)
	{
		imag1[iz+nz*ix]+=num[ix][iz];
		imag2[iz+nz*ix]+=num[ix][iz]/den[ix][iz];
	}
}

void muting(float *seis_kt, int gzbeg, int szbeg, int gxbeg, int sxc, int jgx, 
	int it, int ntd, float vmute, int ng)
/*< muting the direct arrivals >*/
{
	int id, kt;
	float a,b,t0;

	for(id=0;id<ng;id++){
		a=dx*abs(gxbeg+id*jgx-sxc);
		b=dz*(gzbeg-szbeg);
		t0=sqrtf(a*a+b*b)/vmute;
		kt=t0/dt+ntd;// ntd is manually added to obtain the best muting effect.
    		if (it<kt) seis_kt[id]=0.;
	}
}



int main(int argc, char* argv[])
{
	int it, is, i1, i2, tdmute, jsx,jsz,jgx,jgz,sxbeg,szbeg,gxbeg,gzbeg, distx, distz;
	int *sxz, *gxz;
	float tmp, vmax;
	float *wlt, *d2x, *d1z;
	float **v0, **vv, **p, **pz, **px, **vz, **vx, **dcal;
    	sf_file vmodl, imag1, imag2; /* I/O files */

    	sf_init(argc,argv);
#ifdef _OPENMP
    	omp_init();
#endif

    	/*< set up I/O files >*/
    	vmodl = sf_input ("in");   /* velocity model, unit=m/s */
    	imag1 = sf_output("out");  /* output image with correlation imaging condition */ 
    	imag2 = sf_output("imag2");  /* output image with normalized correlation imaging condition */ 

    	/* get parameters for RTM */
    	if (!sf_histint(vmodl,"n1",&nz)) sf_error("no n1");
    	if (!sf_histint(vmodl,"n2",&nx)) sf_error("no n2");
    	if (!sf_histfloat(vmodl,"d1",&dz)) sf_error("no d1");
   	if (!sf_histfloat(vmodl,"d2",&dx)) sf_error("no d2");

    	if (!sf_getfloat("fm",&fm)) sf_error("no fm");	/* dominant freq of ricker */
    	if (!sf_getfloat("dt",&dt)) sf_error("no dt");	/* time interval */

    	if (!sf_getint("nt",&nt))   sf_error("no nt");	/* total modeling time steps */
    	if (!sf_getint("ns",&ns))   sf_error("no ns");	/* total shots */
    	if (!sf_getint("ng",&ng))   sf_error("no ng");	/* total receivers in each shot */
    	if (!sf_getint("nb",&nb))   nb=20; /* thickness of split PML */
	
    	if (!sf_getint("jsx",&jsx))   sf_error("no jsx");/* source x-axis  jump interval  */
    	if (!sf_getint("jsz",&jsz))   jsz=0;/* source z-axis jump interval  */
    	if (!sf_getint("jgx",&jgx))   jgx=1;/* receiver x-axis jump interval */
    	if (!sf_getint("jgz",&jgz))   jgz=0;/* receiver z-axis jump interval */
    	if (!sf_getint("sxbeg",&sxbeg))   sf_error("no sxbeg");/* x-begining index of sources, starting from 0 */
    	if (!sf_getint("szbeg",&szbeg))   sf_error("no szbeg");/* z-begining index of sources, starting from 0 */
    	if (!sf_getint("gxbeg",&gxbeg))   sf_error("no gxbeg");/* x-begining index of receivers, starting from 0 */
    	if (!sf_getint("gzbeg",&gzbeg))   sf_error("no gzbeg");/* z-begining index of receivers, starting from 0 */

	if (!sf_getbool("csdgather",&csdgather)) csdgather=true;/* default, common shot-gather; if n, record at every point*/
	if (!sf_getfloat("vmute",&vmute))   vmute=1500;/* muting velocity to remove the low-freq noise, unit=m/s*/
	if (!sf_getint("tdmute",&tdmute))   tdmute=200;/* number of deleyed time samples to mute */

    	sf_putint(imag1,"n1",nz);
    	sf_putint(imag1,"n2",nx);
    	sf_putfloat(imag1,"d1",dz);
    	sf_putfloat(imag1,"d2",dx);
    	sf_putint(imag2,"n1",nz);
    	sf_putint(imag2,"n2",nx);
    	sf_putfloat(imag2,"d1",dz);
    	sf_putfloat(imag2,"d2",dx);

	_dx=1./dx;
	_dz=1./dz;
	nzpad=nz+2*nb;
	nxpad=nx+2*nb;

	wlt=sf_floatalloc(nt);
	v0=sf_floatalloc2(nz,nx); 	
	vv=sf_floatalloc2(nzpad, nxpad);
	p =sf_floatalloc2(nzpad, nxpad);
	pz=sf_floatalloc2(nzpad, nxpad);
	px=sf_floatalloc2(nzpad, nxpad);
	vz=sf_floatalloc2(nzpad, nxpad);
	vx=sf_floatalloc2(nzpad, nxpad);
	d1z=sf_floatalloc(nzpad);
	d2x=sf_floatalloc(nxpad);
	sxz=sf_intalloc(ns);
	gxz=sf_intalloc(ng);
	dcal=sf_floatalloc2(nt,ng);

	for(it=0;it<nt;it++){
		tmp=SF_PI*fm*(it*dt-1.0/fm);tmp*=tmp;
		wlt[it]=(1.0-2.0*tmp)*expf(-tmp);
	}
	sf_floatread(v0[0],nz*nx,vmodl);
	expand2d(vv, v0);
	memset(p [0],0,nzpad*nxpad*sizeof(float));
	memset(px[0],0,nzpad*nxpad*sizeof(float));
	memset(pz[0],0,nzpad*nxpad*sizeof(float));
	memset(vx[0],0,nzpad*nxpad*sizeof(float));
	memset(vz[0],0,nzpad*nxpad*sizeof(float));
	vmax=v0[0][0];
	for(i2=0; i2<nx; i2++)
	for(i1=0; i1<nz; i1++)
		vmax=SF_MAX(v0[i2][i1],vmax);
	pmlcoeff_init(d1z, d2x, vmax);
	if (!(sxbeg>=0 && szbeg>=0 && sxbeg+(ns-1)*jsx<nx && szbeg+(ns-1)*jsz<nz))	
	{ sf_warning("sources exceeds the computing zone!"); exit(1);}
	sg_init(sxz, szbeg, sxbeg, jsz, jsx, ns);
	distx=sxbeg-gxbeg;
	distz=szbeg-gzbeg;
	if (csdgather)	{
		if (!(gxbeg>=0 && gzbeg>=0 && gxbeg+(ng-1)*jgx<nx && gzbeg+(ng-1)*jgz<nz &&
		(sxbeg+(ns-1)*jsx)+(ng-1)*jgx-distx <nx  && (szbeg+(ns-1)*jsz)+(ng-1)*jgz-distz <nz))	
		{ sf_warning("geophones exceeds the computing zone!"); exit(1);}
	}
	else{
		if (!(gxbeg>=0 && gzbeg>=0 && gxbeg+(ng-1)*jgx<nx && gzbeg+(ng-1)*jgz<nz))	
		{ sf_warning("geophones exceeds the computing zone!"); exit(1);}
	}
	sg_init(gxz, gzbeg, gxbeg, jgz, jgx, ng);

	for(is=0; is<ns; is++)
	{
		for(it=0; it<nt; it++)
		{
			add_source(&sxz[is], p, 1, &wlt[it], true);
			step_forward(p, pz, px, vz, vx, vv, d1z, d2x);

		
			//record_seis(&dcal[it*ng], gxz, sp0, ng);
			//muting(&dcal[it*ng], gzbeg, szbeg, gxbeg, sxbeg+is*jsx, jgx, it, tdmute, vmute, dt, dz, dx, ng);
			//boundary_rw(sp0, &spo[it*4*(nx+nz)], false);
		}
	}

	free(wlt);
	free(*v0); free(v0);
	free(*vv); free(vv);
	free(*p); free(p);
	free(*px); free(px);
	free(*pz); free(pz);
	free(*vx); free(vx);
	free(*vz); free(vz);
	free(d1z);
	free(d2x);
	free(sxz);
	free(gxz);

    	exit(0);
}

