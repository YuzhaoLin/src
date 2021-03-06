/* 2-D two-components wavefield modeling based on original elastic anisotropic displacement
 * wave equation and P-SV separation using divergence and curl operations in 2D TTI media.
 
   Authors: Jiubing Cheng (Tongji University)
     
   Copyright (C) 2012 Tongji University, Shanghai, China 

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
*/
#include <sys/time.h>

#include <rsf.hh>
#include <assert.h>

using namespace std;

/* prepared head files by myself */
#include "_cjb.h"

/*#include "_fd.h" 
 * when _fd.h has declared "m", we met compiling error at "m2=mid.m()";
 * "expected unqualified-id before numeric constant" 
*/
   float  kx, kz, k2, rk, *sinx, *cosx;
#ifndef M
#define M 5   /* 10th-order finite-difference: accuracy is 2*M */
#endif
#ifndef Mix
#define Mix 5   /* order of finite-difference for mix-derivative (2*mix) */
#endif

/* head files aumatically produced from C programs */
extern "C"{
#include "zero.h"
#include "ricker.h"
#include "kykxkztaper.h"
#include "fdcoef.h"
#include "fwpttielastic.h"
#include "seplowrank.h"
#include "sepdivcurl.h"
}

static std::valarray<float> vp, vs, ep, de, th;

/*****************************************************************************************/
int main(int argc, char* argv[])
{
   sf_init(argc,argv);

   timeval time1, time2, time3, time4, time5;
   time_t timeused = 0;

   gettimeofday(&time1, 0);


   iRSF par(0);
   int   ns;
   float dt;

   par.get("ns",ns);
   par.get("dt",dt);

   sf_warning("ns=%d dt=%f",ns,dt);
   sf_warning("read velocity model parameters");

   /* setup I files */
   iRSF vp0, vs0("vs0"), epsi("epsi"), del("del"), the("the");

   /* Read/Write axes */
   int nxv, nzv;
   vp0.get("n1",nzv);
   vp0.get("n2",nxv);

   float az, ax;
   vp0.get("o1",az);
   vp0.get("o2",ax);

   float fx, fz;
   fx=ax*1000.0;
   fz=az*1000.0;

   float dx, dz;
   vp0.get("d1",az);
   vp0.get("d2",ax);
   dz = az*1000.0;
   dx = ax*1000.0;

   /* wave modeling space */
   int nx, nz, nxz;
   nx=nxv;
   nz=nzv;
   nxz=nx*nz;

   vp.resize(nxz);
   vs.resize(nxz);
   ep.resize(nxz);
   de.resize(nxz);
   th.resize(nxz);
 
   vp0>>vp;
   vs0>>vs;
   epsi>>ep;
   del>>de;
   the>>th;

   for(int i=0;i<nxz;i++)
      th[i] *= SF_PI/180.0;

   /* Fourier spectra demension */
   int nkz,nkx,nk;
   nkx=nx;
   nkz=nz;
   nk = nkx*nkz;

   float dkz,dkx,kz0,kx0, s, c;

   dkx=2*SF_PI/dx/nx;
   dkz=2*SF_PI/dz/nz;

   kx0=-SF_PI/dx;
   kz0=-SF_PI/dz;

   float  kx, kz, k2, rk, sx, cx, *sinx, *cosx;
   int    i=0, j=0, k=0, ix, iz;
   
   s = sin(th[i]);
   c = cos(th[i]);

   sinx=sf_floatalloc(nxz);
   cosx=sf_floatalloc(nxz);

   for(ix=0; ix < nkx; ix++)
   {
       kx = kx0+ix*dkx;
       if(kx==0.0) kx=0.0000000001*dkx;

       for (iz=0; iz < nkz; iz++)
       {
            kz = kz0+iz*dkz;
            if(kz==0.0) kz=0.0000000001*dkz;

			// rotatiing according to tilted symmetry axis
		    sx=kx*c+kz*s;
		    cx=kz*c-kx*s;

            k2 = sx*sx+cx*cx;
            rk = sqrt(k2);

            sinx[i] = sx/rk;
            cosx[i] = cx/rk;

            i++;
       }
   }

   /****************begin to calculate wavefield****************/
   /****************begin to calculate wavefield****************/
   /*  wavelet parameter for source definition */
   float A, f0, t0;
   f0=30.0;                  
   t0=0.04;                  
   A=1;                  

   int nxpad, nzpad;
   nxpad=nx+2*M;
   nzpad=nz+2*M;

   sf_warning("fx=%f fz=%f dx=%f dz=%f",fx,fz,dx,dz);
   sf_warning("nx=%d nz=%d nxpad=%d nzpad=%d", nx,nz,nxpad,nzpad);

   int mm=2*M+1;

   float dt2=dt*dt;

   /* source definition */
   int ixs, izs, ixms, izms;
   ixs=nxv/2;
   izs=nzv/2;
   ixms=ixs+M;  /* source's x location */
   izms=izs+M;  /* source's z-location */

   float xs, zs;
   xs=fx+ixs*dx;
   zs=fz+izs*dz;
   sf_warning("source location: (%f, %f)",xs, zs);

   /* setup I/O files */
   oRSF Elasticx("out"),Elasticz("Elasticz");
   oRSF ElasticDivx("ElasticDivx"),ElasticDivz("ElasticDivz");
   oRSF ElasticDivP("ElasticDivP");
   oRSF ElasticCurlSV("ElasticCurlSV");

   Elasticx.put("n1",nkz);
   Elasticx.put("n2",nkx);
   Elasticx.put("d1",dz/1000);
   Elasticx.put("d2",dx/1000);
   Elasticx.put("o1",fz/1000);
   Elasticx.put("o2",fx/1000);

   Elasticz.put("n1",nkz);
   Elasticz.put("n2",nkx);
   Elasticz.put("d1",dz/1000);
   Elasticz.put("d2",dx/1000);
   Elasticz.put("o1",fz/1000);
   Elasticz.put("o2",fx/1000);

   ElasticDivx.put("n1",nkz);
   ElasticDivx.put("n2",nkx);
   ElasticDivx.put("d1",dz/1000);
   ElasticDivx.put("d2",dx/1000);
   ElasticDivx.put("o1",fz/1000);
   ElasticDivx.put("o2",fx/1000);

   ElasticDivz.put("n1",nkz);
   ElasticDivz.put("n2",nkx);
   ElasticDivz.put("d1",dz/1000);
   ElasticDivz.put("d2",dx/1000);
   ElasticDivz.put("o1",fz/1000);
   ElasticDivz.put("o2",fx/1000);

   ElasticDivP.put("n1",nkz);
   ElasticDivP.put("n2",nkx);
   ElasticDivP.put("d1",dz/1000);
   ElasticDivP.put("d2",dx/1000);
   ElasticDivP.put("o1",fz/1000);
   ElasticDivP.put("o2",fx/1000);

   ElasticCurlSV.put("n1",nkz);
   ElasticCurlSV.put("n2",nkx);
   ElasticCurlSV.put("d1",dz/1000);
   ElasticCurlSV.put("d2",dx/1000);
   ElasticCurlSV.put("o1",fz/1000);
   ElasticCurlSV.put("o2",fx/1000);

    float *coeff_2dx=sf_floatalloc(mm);
    float *coeff_2dz=sf_floatalloc(mm);
    float *coeff_1dx=sf_floatalloc(mm);
    float *coeff_1dz=sf_floatalloc(mm);

    coeff2d(coeff_2dx,dx);
    coeff2d(coeff_2dz,dz);
    coeff1dmix(coeff_1dx,dx);
    coeff1dmix(coeff_1dz,dz);

    float **p1=sf_floatalloc2(nzpad, nxpad);
    float **p2=sf_floatalloc2(nzpad, nxpad);
    float **p3=sf_floatalloc2(nzpad, nxpad);

    float **q1=sf_floatalloc2(nzpad, nxpad);
    float **q2=sf_floatalloc2(nzpad, nxpad);
    float **q3=sf_floatalloc2(nzpad, nxpad);

    zero2float(p1, nzpad, nxpad);
    zero2float(p2, nzpad, nxpad);
    zero2float(p3, nzpad, nxpad);

    zero2float(q1, nzpad, nxpad);
    zero2float(q2, nzpad, nxpad);
    zero2float(q3, nzpad, nxpad);

    sf_warning("==================================================");
    sf_warning("==  Porpagation Using Elastic anisotropic Eq.   ==");
    sf_warning("==================================================");

    std::valarray<float> x(nxz);
    std::valarray<float> y(nxz);
    std::valarray<float> z(nxz);

    float **vpp, **vss, **epp, **dee, **thee;

    vpp=sf_floatalloc2(nz,nx);
    vss=sf_floatalloc2(nz,nx);
    epp=sf_floatalloc2(nz,nx);
    dee=sf_floatalloc2(nz,nx);
    thee=sf_floatalloc2(nz,nx);

    k=0;
    for(i=0;i<nx;i++)
    for(j=0;j<nz;j++)
    {
       vpp[i][j]=vp[k];
       vss[i][j]=vs[k];
       epp[i][j]=ep[k];
       dee[i][j]=de[k];
       thee[i][j]=th[k];
       k++;
    }

    float *pp, *qq;
    pp=sf_floatalloc(nxz);
    qq=sf_floatalloc(nxz);

    int ii, jj, im, jm;

    int *ijkx = sf_intalloc(nkx);
    int *ijkz = sf_intalloc(nkz);

    ikxikz(ijkx, ijkz, nkx, nkz);

    int iflag=0;

   gettimeofday(&time2, 0);
   timeused=time2.tv_sec - time1.tv_sec;
   sf_warning("CPU time for prereparing for modeling: %d(second)",timeused);

    for(int it=0;it<ns;it++)
    {
	float t=it*dt;

	if(it%50==0)
		sf_warning("Elastic: it= %d",it);

        // 2D exploding force source (e.g., Wu's PhD)
	    /*
        for(i=-1;i<=1;i++)
        for(j=-1;j<=1;j++)
        {
             if(fabs(i)+fabs(j)==2)
             {
                  p2[ixms+i][izms+j]+=i*Ricker(t, f0, t0, A);
                  q2[ixms+i][izms+j]+=j*Ricker(t, f0, t0, A);
             }
        }
		*/
        for(i=-1;i<=1;i++)
        for(j=-1;j<=1;j++)
        {
             if(fabs(i)+fabs(j)==1)
             {
                  p2[ixms+i][izms+j]+=i*Ricker(t, f0, t0, A);
                  q2[ixms+i][izms+j]+=j*Ricker(t, f0, t0, A);
             }
        }

        /* fwpttielastic: forward-propagating using original elastic equation of displacement in TTI media*/
        fwpttielastic(dt2, p1, p2, p3, q1, q2, q3, coeff_2dx, coeff_2dz, coeff_1dx, coeff_1dz,
                      dx, dz, nx, nz, nxpad, nzpad, vpp, vss, epp, dee, thee);

        /******* output wavefields: component and divergence *******/
        if(it==ns-1)
	{
        gettimeofday(&time3, 0);
        timeused=time3.tv_sec - time2.tv_sec;
        sf_warning("CPU time for wavefield modeling: %d(second)",timeused);

              k=0;
	      for(i=0;i<nx;i++)
              {
                   im=i+M;
		   for(j=0;j<nz;j++)
		   {
                       jm=j+M;

                       x[k] = pp[k] = p3[im][jm];
                       y[k] = qq[k] = q3[im][jm];

                       k++;      
		    }
              }// i loop
              Elasticx<<x;
              Elasticz<<y;


              /* separate qP wave  */
              sf_warning("separate qP-wave based on divergence"); 
              sepdiv2d(sinx,pp,ijkx,ijkz,nx,nz,nxz,nk,iflag);
              sepdiv2d(cosx,qq,ijkx,ijkz,nx,nz,nxz,nk,iflag);

              for(i=0;i<nxz;i++)
		          z[i]=pp[i];
              ElasticDivx<<z;

              for(i=0;i<nxz;i++)
		          z[i]=qq[i];
              ElasticDivz<<z;

              for(i=0;i<nxz;i++)
		          z[i]=pp[i]+qq[i];

              ElasticDivP<<z;
          
              k=0;
	      for(i=0;i<nx;i++)
              {
                   im=i+M;
		   for(j=0;j<nz;j++)
		   {
                       jm=j+M;

                       pp[k] = p3[im][jm];
                       qq[k] = q3[im][jm];

                       k++;      
		    }
              }// i loop

              /* separate qSV wave  */
              sf_warning("separate qSV-wave based on Curl"); 
              sepdiv2d(cosx, pp,ijkx,ijkz,nx,nz,nxz,nk,iflag);
              sepdiv2d(sinx, qq,ijkx,ijkz,nx,nz,nxz,nk,iflag);

              for(i=0;i<nxz;i++)
		        z[i]=pp[i]-qq[i];

              ElasticCurlSV<<z;

              for(i=0;i<nxz;i++)
		          z[i]=pp[i]+qq[i];

              ElasticDivP<<z;
          
              k=0;
	      for(i=0;i<nx;i++)
              {
                   im=i+M;
		   for(j=0;j<nz;j++)
		   {
                       jm=j+M;

                       pp[k] = p3[im][jm];
                       qq[k] = q3[im][jm];

                       k++;      
		    }
              }// i loop

              /* separate qSV wave  */
              sf_warning("separate qSV-wave based on Curl"); 
              sepdiv2d(cosx, pp,ijkx,ijkz,nx,nz,nxz,nk,iflag);
              sepdiv2d(sinx, qq,ijkx,ijkz,nx,nz,nxz,nk,iflag);

              for(i=0;i<nxz;i++)
		        z[i]=pp[i]-qq[i];

              ElasticCurlSV<<z;

        gettimeofday(&time4, 0);
        timeused=time4.tv_sec - time3.tv_sec;
        sf_warning("CPU time for one-step qP/qSV separation: %d(second)",timeused);

         }/* (it+1)%ntstep==0 */

         /**************************************/
 	 for(i=0,ii=M;i<nx;i++,ii++)
	    for(j=0,jj=M;j<nz;j++,jj++)
	    {
		p1[ii][jj]=p2[ii][jj];	
		p2[ii][jj]=p3[ii][jj];	

		q1[ii][jj]=q2[ii][jj];	
		q2[ii][jj]=q3[ii][jj];	
	    }

    }/* it loop */

    free(pp);
    free(qq);

    free(sinx);
    free(cosx);

    free(*p1);
    free(*p2);
    free(*p3);
    free(*q1);
    free(*q2);
    free(*q3);

    free(*vpp);
    free(*vss);
    free(*epp);
    free(*dee);
    free(*thee);

    free(coeff_2dx);
    free(coeff_2dz);
    free(coeff_1dx);
    free(coeff_1dz);

    free(ijkx);
    free(ijkz);
exit(0);
}

