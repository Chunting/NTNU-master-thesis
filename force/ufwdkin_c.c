#include <math.h>
//#include "ur5.h"
void  ufwdkin(tfrotype * res,double * v,double *apar, double * dpar){
double a2,a3,d1,d4,d5,d6,v1,v2,v3,v4,v5,v6,t2,t3,t4,t5,t6,t7,t8,t9,t10,t11,t12,t13,t14,t15,t16,t17,t18,t19,t20;
double t21,t22,t23,t24,t25,t26,t27,t28,t29,t30,t31,t32,t33,t34,t35,t36,t37,t38,t39,t40,t41;
double t42,t43,t44,t45,t46,t47,t48,t49,t50,t51,t52,t53,t54,t55,t56;
double *p,*p1;
a2 = apar[1];
a3 = apar[2];
d1 = dpar[0];
d4 = dpar[3];
d5 = dpar[4];
d6 = dpar[5];
v1 = v[0];
v2 = v[1];
v3 = v[2];
v4 = v[3];
v5 = v[4];
v6 = v[5];
t2 = cos(v1);
t3 = cos(v2);
t4 = sin(v3);
t5 = cos(v3);
t6 = sin(v2);
t7 = cos(v4);
t8 = t2*t3*t4;
t9 = t2*t5*t6;
t10 = t8+t9;
t11 = sin(v4);
t12 = t2*t3*t5;
t30 = t2*t4*t6;
t13 = t12-t30;
t14 = sin(v6);
t15 = sin(v1);
t16 = cos(v6);
t17 = cos(v5);
t18 = t3*t5*t15;
t38 = t4*t6*t15;
t19 = t18-t38;
t20 = t3*t4*t15;
t21 = t5*t6*t15;
t22 = t20+t21;
t23 = sin(v5);
t24 = t3*t4;
t25 = t5*t6;
t26 = t24+t25;
t27 = t3*t5;
t45 = t4*t6;
t28 = t27-t45;
t29 = t15*t23;
t31 = t7*t13;
t51 = t10*t11;
t52 = t31-t51;
t32 = t17*t52;
t33 = t29+t32;
t34 = t7*t10;
t35 = t11*t13;
t36 = t34+t35;
t37 = t7*t22;
t39 = t11*t19;
t40 = t37+t39;
t41 = t7*t19;
t53 = t11*t22;
t42 = t41-t53;
t43 = t2*t23;
t44 = t43-t17*t42;
t46 = t7*t28;
t56 = t11*t26;
t47 = t46-t56;
t48 = t7*t26;
t49 = t11*t28;
t50 = t48+t49;
t54 = t15*t17;
t55 = t54-t23*t52;
p=res->R;
p1=res->O;
*p++=t16*t33-t14*t36;
*p++=-t14*t33-t16*t36;
*p++=t55;
*p1++=d5*(t34+t11*(t12-t30))+d4*t15+d6*t55+a2*t2*t3+a3*t2*t3*t5-a3*t2*t4*t6;
*p++=-t14*t40-t16*t44;
*p++=-t16*t40+t14*t44;
*p++=-t2*t17-t23*t42;
*p1++=d5*(t37+t11*(t18-t38))-d4*t2-d6*(t2*t17+t23*t42)+a2*t3*t15+a3*t3*t5*t15-a3*t4*t6*t15;
*p++=t14*t47+t16*t17*t50;
*p++=t16*t47-t14*t17*t50;
*p++=-t23*t50;
*p1++=d1+a2*t6-d5*t47+a3*t3*t4+a3*t5*t6-d6*t23*t50;
}
