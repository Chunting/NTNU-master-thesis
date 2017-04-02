#include "force.h"

#include "ujac_c.c"
#include "ufwdkin_c.c"

int initFT = 1;
int forceSleepTime = 10000; //usleep(10000) ~ 90 Hz --> run a bit faster than the fastest human reaction time -- currently involuntary muscle contractions at 24ms ~ 41,6 Hz
//Fastest possible hardware update rate for the UR5 is set at 8ms ~ 125 Hz equivalent to usleep(8000)
double rawFTdata[6];
double biasTF[3]; // Tool frame (or TCP frame)


void *getFTData(void *arg)
{
	int socketHandle;			/* Handle to UDP socket used to communicate with Net F/T. */
	struct sockaddr_in addr;		/* Address of Net F/T. */
	struct hostent *he;			/* Host entry for Net F/T. */
	byte request[8];			/* The request data sent to the Net F/T. */
	RESPONSE resp;				/* The structured response received from the Net F/T. */
	byte response[36];			/* The raw response data received from the Net F/T. */
	int i;					/* Generic loop/array index. */
	int err;				/* Error status of operations. */
	//char const *AXES[] = { "Fx", "Fy", "Fz", "Tx", "Ty", "Tz" };	/* The names of the force and torque axes. */
	


	/* Calculate number of samples, command code, and open socket here. */
	socketHandle = socket(AF_INET, SOCK_DGRAM, 0);
	if (socketHandle == -1)
	{
		exit(1);
	}

	*(uint16*)&request[0] = htons(0x1234); 	/* standard header. */
	*(uint16*)&request[2] = htons(COMMAND); 	/* per table 9.1 in Net F/T user manual. */
	*(uint32*)&request[4] = htonl(NUM_SAMPLES); /* see section 9.1 in Net F/T user manual. */

	/* Sending the request. */
	he = gethostbyname("10.42.0.8");


	memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(PORT);

	err = connect(socketHandle, (struct sockaddr *)&addr, sizeof(addr));
	if (err == -1)
	{
		exit(2);
	}
	send(socketHandle, request, 8, 0);

	
	int scale_factor = 1000000; //Scales down to Newton [N] and Newtonmeter [Nm]
	while(initFT)
	{
		/* Receiving the response. */
		recv( socketHandle, response, 36, 0 );
		resp.rdt_sequence = ntohl(*(uint32*)&response[0]);
		resp.ft_sequence = ntohl(*(uint32*)&response[4]);
		resp.status = ntohl(*(uint32*)&response[8]);
		for( i = 0; i < 6; i++ )
		{
			resp.FTData[i] = ntohl(*(int32*)&response[12 + i * 4]);
		}


		double Fx = (double)resp.FTData[0]/scale_factor;
		double Fy = (double)resp.FTData[1]/scale_factor;
		double Fz = (double)resp.FTData[2]/scale_factor;
		double Tx = (double)resp.FTData[3]/scale_factor;
		double Ty = (double)resp.FTData[4]/scale_factor;
		double Tz = (double)resp.FTData[5]/scale_factor;
	
		rawFTdata[0] = Fx; rawFTdata[1] = Fy; rawFTdata[2] = Fz; rawFTdata[3] = Tx; rawFTdata[4] = Ty; rawFTdata[5] = Tz;
		usleep(3800); // 4000 microseconds ~ 250 Hz is current FT broadcast frequency
		//usleep830 ~ 1200 Hz --> run a bit faster than FT broadcast frequency -- currently 1000 Hz
	}
}

void startFT(pthread_t *forceID)
{
	//pthread_t forceID;
	if(pthread_create(forceID, NULL, &getFTData, NULL))
	{
		std::cout << "Error: force/torque thread not created!" << std::endl;
		//return -1;
	}
	std::cout << "Broadcasting force/torque data!" << std::endl;
	usleep(100000);
}

void stopFT(pthread_t *forceID)
{
	initFT = 0;
	pthread_join(*forceID, NULL);
	std::cout << "Force thread joined - stopping force/torque data acquisition!" << std::endl;
	usleep(100000);
}

/*
void rot_z(double angle, double rot[9])
{
    rot[0] = cos(angle);  rot[1] = -sin(angle); rot[2] = 0;
    rot[3] = sin(angle);  rot[4] = cos(angle);  rot[5] = 0;
    rot[6] = 0;           rot[7] = 0;           rot[8] = 1;
}
*/

void rotate(gsl_vector *res,gsl_matrix *R, gsl_vector *inp,gsl_vector *t1,gsl_vector *t2)
{
	t1->data=inp->data;
	t2->data=res->data;
	gsl_blas_dgemv(CblasNoTrans ,1.0,R, t1,0.0,t2); 
	t1->data=&inp->data[3];
	t2->data=&res->data[3];
	gsl_blas_dgemv(CblasNoTrans ,1.0,R, t1,0.0,t2); 
}

void vector_trans_base_tool(std::vector<double> q, double vector_in[3], double vector_out[3])
{	
	gsl_matrix *R = gsl_matrix_calloc(3,3);
	gsl_vector *O = gsl_vector_alloc(3);
	
	double apar[6] = {0,-0.42500,-0.39225,0,0,0};
	double dpar[6] = {0.089159,0,0,0.10915,0.09465,0.0823};

	tfrotype tfkin;
	R->data=tfkin.R;
	O->data=tfkin.O;
	ufwdkin(&tfkin,q.data(),apar,dpar);

	for (int i=0; i<3; i++)
	{
		for (int j=0; j<3 ;j++)
		{
			vector_out[i] += vector_in[j]*gsl_matrix_get(R, j, i);
		}
	}
	
}

void solveInverseJacobian(std::vector<double> q, double vw[6], double qd[6])
{
	gsl_vector *x = gsl_vector_alloc(6);
	gsl_vector *vw_ = gsl_vector_alloc(6);
	gsl_vector *t1 = gsl_vector_alloc(3);
	gsl_vector *t2 = gsl_vector_alloc(3);
	gsl_vector *vw_w = gsl_vector_alloc(6);
	gsl_matrix *R = gsl_matrix_alloc(3,3);
	gsl_matrix *A = gsl_matrix_alloc(6,6);
	gsl_permutation *p = gsl_permutation_alloc(6);
	
	int signum;
	double apar[6] = {0,-0.42500,-0.39225,0,0,0};
	double dpar[6] = {0.089159,0,0,0.10915,0.09465,0.0823};
	double fqd;
	double sat = 0;
	
	gsl_vector_set(vw_,0,vw[0]);
	gsl_vector_set(vw_,1,vw[1]);
	gsl_vector_set(vw_,2,vw[2]);
	gsl_vector_set(vw_,3,vw[3]);
	gsl_vector_set(vw_,4,vw[4]);
	gsl_vector_set(vw_,5,vw[5]);
	
	
	tfrotype tfkin;
	R->data=tfkin.R;
	ufwdkin(&tfkin,q.data(),apar,dpar);
	rotate(vw_w,R,vw_,t1,t2);
	
	
	ujac(A->data,q.data(),apar,dpar);
	
	gsl_linalg_LU_decomp(A,p,&signum);

	gsl_linalg_LU_solve(A,p,vw_w,x);
	for (int k=0;k<6;k++)
	{
		qd[k] = gsl_vector_get(x,k); 
		fqd = fabs(qd[k]);
		if (qd[k] > 1 || qd[k] < -1)
		{
			sat = fqd > sat ? fqd : sat;   //Note: '?:' is a ternary operator
		}
	}
	if(sat > 0)
	{
		for (int i = 0; i < 6; i++)
		{
			qd[i] = qd[i]/sat*1;
		}
	}
}

void forceControl(UrDriver *ur5, std::condition_variable *rt_msg_cond_, int run_time, int force_mode, double user_parameters[6], double f_ref, double t_ref)
{
	pthread_t forceID;
	startFT(&forceID);	
	
	
	std::cout << "Force control initiated - starting data logging ..." << std::endl;
	std::ofstream forcelog;
	forcelog.open("../data/logs/forcelog", std::ofstream::out);
	//Control system for translation forces
	double integrator_Fx = 0, integrator_Fy = 0, integrator_Fz = 0;
	double derivator_Fx = 0, derivator_Fy = 0, derivator_Fz = 0;
	double Kp = 0, Ki = 0, Kd = 0;
	
	double error_Fx = 0, error_Fy = 0, error_Fz = 0;
	double prior_error_Fx = 0, prior_error_Fy = 0, prior_error_Fz = 0;
	double u_Fx = 0, u_Fy = 0, u_Fz = 0;
	
	//Control system for rotational forces
	double integrator_Tx = 0, integrator_Ty = 0, integrator_Tz = 0;
	double derivator_Tx = 0, derivator_Ty = 0, derivator_Tz = 0;
	double Kp_T = 0, Ki_T = 0, Kd_T = 0;
	
	double error_Tx = 0, error_Ty = 0, error_Tz = 0;
	double prior_error_Tx = 0, prior_error_Ty = 0, prior_error_Tz = 0;
	double u_Tx = 0, u_Ty = 0, u_Tz = 0;
	
	//=======================================================================
	double references[6] = {0,0,0,0,0,0}; //ref_Fx, ref_Fy, ref_Fz, ref_Tx, ref_Ty, ref_Tz
	double speed[6] = {0,0,0,0,0,0};
	double vw[6] = {0,0,0,0,0,0};

	//End-effector tool bias: a constant mounting bias and a dynamic gravity bias.
	double bias_tool_WF[3] = {0, 0, -1};//Weight of end-effector tool in world frame ('_WF').
	double bias_tool_TF[3]; //Weight of end-effector tool in tool frame ('_TF').
	double bias_mounting[3];
	double biasForce[3];
	
	double biasTorque[3] = {rawFTdata[3], rawFTdata[4], rawFTdata[5]};
	
	double theta;
	double radius_current;
	double radius_min = 0.35;//unit [m] 
	double radius_max = 0.7;
	double height_min = -0.22;
	double height_max = 0.4;
	double angle_max = M_PI/2;//unit [rad]
	
	double correction_vector_WF[3];
   	double correction_vector_TF[3];
   	double correction_vector_scale = 500;
	
	double startTime = ur5->rt_interface_->robot_state_->getTime();
	std::vector<double> sq = ur5->rt_interface_->robot_state_->getQActual();
	vector_trans_base_tool(sq, bias_tool_WF, bias_tool_TF);
	
	for (int i=0; i<3; i++)
	{
		bias_mounting[i] = fabs(rawFTdata[i]) + bias_tool_TF[i];
		std::cout << "bias_mounting[" << i << "] = " << bias_mounting[i] << std::endl;
	}
	
	
	int i = 0; 
	int iter = run_time/0.008;
	
	//Doublecheck PID controller tuning
	Kp = 0.005;//0.005; //Original: -0.005 // Tuned: -0.008 or 0.005
	Ki = 0.0003;//0.0002; //Original: -0.000025 //Tuned: -0.00085 or -0.0003
	Kd = 0.0002;//0.0002;//0.000045; //Original: -0.000025 //Tuned: -0.00045
	
	Kp_T = 0.03;//0.3; //Original: -0.005;
	Ki_T = 0.005;//0.003; //Original: -0.000025;
	Kd_T = 0.0005;//0.000045;
	
	gsl_matrix *R = gsl_matrix_calloc(3,3);
	gsl_vector *O = gsl_vector_alloc(3);
	double apar[6] = {0,-0.42500,-0.39225,0,0,0};
	double dpar[6] = {0.089159,0,0,0.10915,0.09465,0.0823};
	
	double disturbances[6] = {0,0,0,0,0,0};
	srand(time(NULL));
	double randomDuration = rand() % 375 + 125;
	double randomDisturbances[6] = {0,0,0,0,0,0};
	
	//double testTime = 125*4; //4 seconds test
	//double test_force = 10; //Newton step responce
	
	std::cout << "======================== FORCE CONTROL ACTIVE ========================" << std::endl;
	
	while(i<iter)
	{
	
		std::mutex msg_lock;
		std::unique_lock<std::mutex> locker(msg_lock);
		while (!ur5->rt_interface_->robot_state_->getDataPublished())
		{
			rt_msg_cond_->wait(locker);
		}
		
		double timeStamp = ur5->rt_interface_->robot_state_->getTime();
		double elapsTime = timeStamp-startTime;
				  
		std::vector<double> q = ur5->rt_interface_->robot_state_->getQActual();
   		tfrotype tfkin;
		R->data=tfkin.R;
		O->data=tfkin.O;
		ufwdkin(&tfkin,q.data(),apar,dpar);
		
		bias_tool_TF[0] = bias_tool_TF[1] = bias_tool_TF[2] = 0;
		vector_trans_base_tool(q, bias_tool_WF, bias_tool_TF);
		
		for(int j=0; j<3; j++)
		{
			biasForce[j] = bias_mounting[j] - bias_tool_TF[j];
		}
		
		
		double Forces[3] = {rawFTdata[0]+biasForce[0], rawFTdata[1]+biasForce[1], rawFTdata[2]+biasForce[2]};
		double Torques[3] = {rawFTdata[3]-biasTorque[0], rawFTdata[4]-biasTorque[1], rawFTdata[5]-biasTorque[2]};

   		//TESTING
		/*
		if (testTime > 50 && testTime < 100)
		{
			Forces[0] = test_force;
			//Forces[1] = test_force;
			//Forces[2] = test_force;
		}
		if (testTime < 0)
		{
			ur5->setSpeed(0,0,0,0,0,0,1);
			break;
		}
		testTime = testTime -1;
		*/
		
		theta = atan2(gsl_vector_get(O,1), gsl_vector_get(O,0));
		radius_current = sqrt(pow(gsl_vector_get(O,0), 2.0)+pow(gsl_vector_get(O,1), 2.0));
   		
   		error_Fx = error_Fx = error_Fx = 0;
   		//FORCE ERROR UPDATES
   		if(fabs(Forces[0]) < 1 && fabs(Forces[1]) < 1 && fabs(Forces[2]) < 1)
   		{
			error_Fx = error_Fx/1.2; //Gentle step-down
			error_Fy = error_Fy/1.2;
			error_Fz = error_Fz/1.2;
			
			integrator_Fx = integrator_Fx/1.2;
			integrator_Fy = integrator_Fy/1.2;
			integrator_Fz = integrator_Fz/1.2;
			
			
		}
		else
		{	
			error_Fx = references[0] + Forces[0];
			error_Fy = references[1] + Forces[1];
			error_Fz = references[2] + Forces[2];
			
			//SAFETY MECHANISM
			if(radius_current > radius_max)
			{
				correction_vector_WF[0] = -radius_current*cos(theta);
	   			correction_vector_WF[1] = -radius_current*sin(theta);
	   			correction_vector_WF[2] = 0;
	   			
	   			correction_vector_TF[0] = correction_vector_TF[1] = correction_vector_TF[2] = 0;
	   			vector_trans_base_tool(q, correction_vector_WF, correction_vector_TF);
	   			
	   			error_Fx += correction_vector_TF[0]*fabs(radius_current-radius_max)*correction_vector_scale;
	   			error_Fy += correction_vector_TF[1]*fabs(radius_current-radius_max)*correction_vector_scale;
	   			error_Fz += correction_vector_TF[2]*fabs(radius_current-radius_max)*correction_vector_scale;
			}
			else if(radius_current < radius_min)
			{
				correction_vector_WF[0] = radius_current*cos(theta);
	   			correction_vector_WF[1] = radius_current*sin(theta);
	   			correction_vector_WF[2] = 0;
	   			
	   			correction_vector_TF[0] = correction_vector_TF[1] = correction_vector_TF[2] = 0;
	   			vector_trans_base_tool(q, correction_vector_WF, correction_vector_TF);
	   			
	   			error_Fx += correction_vector_TF[0]*fabs(radius_current-radius_min)*correction_vector_scale;
	   			error_Fy += correction_vector_TF[1]*fabs(radius_current-radius_min)*correction_vector_scale;
	   			error_Fz += correction_vector_TF[2]*fabs(radius_current-radius_min)*correction_vector_scale;
			}
	   		if(gsl_vector_get(O,2) > height_max)
	   		{
	   			correction_vector_WF[0] = 0;
	   			correction_vector_WF[1] = 0;
	   			correction_vector_WF[2] = -1;
	   			
	   			correction_vector_TF[0] = correction_vector_TF[1] = correction_vector_TF[2] = 0;
	   			vector_trans_base_tool(q, correction_vector_WF, correction_vector_TF);
	   			
	   			error_Fx += correction_vector_TF[0]*(fabs(gsl_vector_get(O,2))-fabs(height_max))*correction_vector_scale;
	   			error_Fy += correction_vector_TF[1]*(fabs(gsl_vector_get(O,2))-fabs(height_max))*correction_vector_scale;
	   			error_Fz += correction_vector_TF[2]*(fabs(gsl_vector_get(O,2))-fabs(height_max))*correction_vector_scale;	
	   		}
	   		else if(gsl_vector_get(O,2) < height_min)
	   		{
	   			correction_vector_WF[0] = 0;
	   			correction_vector_WF[1] = 0;
	   			correction_vector_WF[2] = 1;
	   			
	   			correction_vector_TF[0] = correction_vector_TF[1] = correction_vector_TF[2] = 0;
	   			vector_trans_base_tool(q, correction_vector_WF, correction_vector_TF);
	   			
	   			error_Fx += correction_vector_TF[0]*(fabs(gsl_vector_get(O,2))-fabs(height_min))*correction_vector_scale;
	   			error_Fy += correction_vector_TF[1]*(fabs(gsl_vector_get(O,2))-fabs(height_min))*correction_vector_scale;
	   			error_Fz += correction_vector_TF[2]*(fabs(gsl_vector_get(O,2))-fabs(height_min))*correction_vector_scale;	
	   		}
		}
		
		//TORQUE ERROR UPDATES
		if(fabs(Torques[0]) < 0.5 && fabs(Torques[1]) < 0.5 && fabs(Torques[2]) < 0.5)
		{
			error_Tx = error_Tx/1.2; //Gentle step-down
			error_Ty = error_Ty/1.2;
	   		error_Tz = error_Tz/1.2;
	   		
	   		integrator_Tx = integrator_Tx/1.2;
			integrator_Ty = integrator_Ty/1.2;
			integrator_Tz = integrator_Tz/1.2;
		}
		else
		{
			error_Tx = Torques[0];
			error_Ty = Torques[1];
	   		error_Tz = Torques[2];
	   		
	   		//SAFETY MECHANISM
	   		if(fabs(q[3]) > fabs(sq[3])+angle_max)
			{
				error_Tx += copysign((fabs(q[3])-(fabs(sq[3])+angle_max))*10, -q[3]);
			}
			if(fabs(q[4]) > fabs(sq[4])+angle_max)
			{
				error_Ty += copysign((fabs(q[4])-(fabs(sq[4])+angle_max))*10, -q[4]);
			}
			if(fabs(q[5]) > fabs(sq[5])+angle_max)
			{
				error_Tz += copysign((fabs(q[5])-(fabs(sq[5])+angle_max))*10, -q[5]);
			}
		}


		//SAFETY MECHANISM 
		if(fabs(error_Fx) > 50 || fabs(error_Fy) > 50 || fabs(error_Fz) > 50)
		{
			std::cout << "============================= STOPPING! ============================" << std::endl;
			ur5->halt();
			std::cout << "Force levels too large - force control halted!" << std::endl;
			break;
		}
		
		if(fabs(error_Tx) > 25 || fabs(error_Ty) > 25 || fabs(error_Tz) > 25)
		{
			std::cout << "============================= STOPPING! ============================" << std::endl;
			ur5->halt();
			std::cout << "Torque levels too large - force control halted!" << std::endl;
			break;
		}
		
		if(fabs(q[3]) > fabs(sq[3])+angle_max*2 || fabs(q[4]) > fabs(sq[4])+angle_max*2 || fabs(q[5]) > fabs(sq[5])+angle_max*2)
		{
			std::cout << "============================= STOPPING! ============================" << std::endl;
			ur5->halt();
			std::cout << "Joint angle too large  - force control halted!" << std::endl;
			break;
		}
		
		//GENERATING RANDOM DISTURANCES
		if (force_mode == 3 && randomDuration < 1) 
		{
			for (int j = 0; j<6; j++)
			{
				randomDisturbances[j] = (rand() % 30)-15; //Force between -15 and 14 Newton 
			}
			randomDuration = rand() % 500 + 250; //Duration between 2-4 seconds
		}
		
		
		//=============== CONTROLLER =====================
		//Translational forces - 3DOF
		integrator_Fx = integrator_Fx + error_Fx;
		integrator_Fy = integrator_Fy + error_Fy;
		integrator_Fz = integrator_Fz + error_Fz;
		
		derivator_Fx = error_Fx - prior_error_Fx;
		derivator_Fy = error_Fy - prior_error_Fy;
		derivator_Fz = error_Fz - prior_error_Fz;
		
		u_Fx = Kp*error_Fx + Ki*integrator_Fx + Kd*derivator_Fx;
		u_Fy = Kp*error_Fy + Ki*integrator_Fy + Kd*derivator_Fy;
		u_Fz = Kp*error_Fz + Ki*integrator_Fz + Kd*derivator_Fz;
		
		//Rotational torques - 3DOF
		integrator_Tx = integrator_Tx + error_Tx;
		integrator_Ty = integrator_Ty + error_Ty;
		integrator_Tz = integrator_Tz + error_Tz;
		
		derivator_Tx = error_Tx - prior_error_Tx;
		derivator_Ty = error_Ty - prior_error_Ty;
		derivator_Tz = error_Tz - prior_error_Tz;
		
		u_Tx = Kp_T*error_Tx + Ki_T*integrator_Tx + Kd_T*derivator_Tx;
		u_Ty = Kp_T*error_Ty + Ki_T*integrator_Ty + Kd_T*derivator_Ty;
		u_Tz = Kp_T*error_Tz + Ki_T*integrator_Tz + Kd_T*derivator_Tz;
		
		//SAFETY MECHANISM 
		if(fabs(u_Fx) > 5 || fabs(u_Fy) > 5 || fabs(u_Fz) > 5) //Ensures passitivity in system
		{
			std::cout << "============================= STOPPING! ============================" << std::endl;
			ur5->halt();
			std::cout << "Force control input too large - stopping force control!" << std::endl;
			break;
		}
		
		if(fabs(u_Tx) > 5 || fabs(u_Ty) > 5 || fabs(u_Tz) > 5)
		{
			std::cout << "============================= STOPPING! ============================" << std::endl;
			ur5->halt();
			std::cout << "Torque control input too large - stopping force control!" << std::endl;
			break;
		}
		
		//Clear all referances
		for (int g = 0; g<6; g++)
		{
			references[g] = 0;
		}
		
		//FORCE MODES
		if(force_mode == 1) //Compliance mode
		{
			vw[0] = u_Fx;
			vw[1] = u_Fy; 
			vw[2] = u_Fz; 
			vw[3] = u_Tx;
			vw[4] = u_Ty;
			vw[5] = u_Tz;
		}
		if(force_mode == 2) //Buoyancy mode
		{
			vw[0] = u_Fx;
			vw[1] = u_Fy;
			vw[2] = u_Fz; 
			vw[3] = 0;//u_Tx;
			vw[4] = 0;//u_Ty;
			vw[5] = 0;//u_Tz;
			
			double buoyancy_vector_WF[3] = {0, 0, user_parameters[1]};
			double buoyancy_vector_TF[3]; 
			vector_trans_base_tool(q, buoyancy_vector_WF, buoyancy_vector_TF);
			
			references[0] = buoyancy_vector_TF[0];
			references[1] = buoyancy_vector_TF[1];
			references[2] = buoyancy_vector_TF[2];
		}
		if(force_mode == 3) //Random mode
		{
			vw[0] = u_Fx;
			vw[1] = u_Fy; 
			vw[2] = u_Fz; 
			vw[3] = 0;//u_Tx;
			vw[4] = 0;//u_Ty;
			vw[5] = 0;//u_Tz;
			
			for (int j = 0; j<6; j++)
			{
				if((copysign(1.0, disturbances[j]) != copysign(1.0, randomDisturbances[j])) && (fabs(disturbances[j]) > 0.001))
				{
					disturbances[j] = disturbances[j]/1.2; //Gentle step-down
				}
				else if(disturbances[j] < (randomDisturbances[j]*user_parameters[j]))
				{
					disturbances[j] += fabs((randomDisturbances[j]*user_parameters[j])-disturbances[j])/fabs((randomDisturbances[j]*user_parameters[j])); //Gentle step-up
				}
				references[j] = disturbances[j];
			}
			
			randomDuration -= 1;
		}
		if(force_mode == 4) //Resistance mode
		{	
			vw[0] = u_Fx;
			vw[1] = u_Fy;
			vw[2] = u_Fz;
			vw[3] = 0;//u_Tx;
			vw[4] = 0;//u_Ty;
			vw[5] = 0;//u_Tz;
			
			for (int j = 0; j<6; j++)
			{
				references[j] = -(fabs(Forces[j]*user_parameters[j]))*copysign(1.0, Forces[j]);
				if (user_parameters[j] == 1)
				{
					vw[j] = 0;
				}
			}
			
		}
		if(force_mode == 5) //2-plane mode (covered by resistance mode?)
		{
			vw[0] = u_Fx;
			vw[1] = 0;
			vw[2] = u_Fz;
			vw[3] = 0;
			vw[4] = 0;
			vw[5] = 0;
		}
		
		//FUTURE WORK
		//Suggested modes: (less agressive controller parameters), 1-plane mode, functional reach
		
		solveInverseJacobian(q, vw, speed);
		
		ur5->rt_interface_->robot_state_->setDataPublished();
		ur5->setSpeed(speed[0], speed[1], speed[2], speed[3], speed[4], speed[5], 100); //acc=20
		
		prior_error_Fx = error_Fx; 
		prior_error_Fy = error_Fy;
		prior_error_Fz = error_Fz;
		
		prior_error_Tx = error_Tx; 
		prior_error_Ty = error_Ty;
		prior_error_Tz = error_Tz;
		
		
		forcelog << elapsTime << " " << speed[0] << " " << speed[1] << " " << speed[2] << " " << speed[3] << " " << speed[4] << " " << speed[5] << " " << q[0] << " " << q[1] << " " << q[2] << " " << q[3] << " " << q[4] << " " << q[5] << " " << rawFTdata[0] << " " << rawFTdata[1] << " " << rawFTdata[2] << " " << rawFTdata[3] << " " << rawFTdata[4] << " " << rawFTdata[5] << " " << Forces[0] << " " << Forces[1] << " " << Forces[2] << " " << Torques[0] << " " << Torques[1] << " " << Torques[2] << " " << error_Fx << " " << error_Fy << " " << error_Fz << " " << error_Tx << " " << error_Ty << " " << error_Tz << " " << u_Fx << " " << u_Fy << " " << u_Fz << " " << u_Tx << " " << u_Ty << " " << u_Tz << " " << biasForce[0] << " " << biasForce[1] << " " << biasForce[2] << " "  << bias_tool_TF[0] << " " << bias_tool_TF[1] << " " << bias_tool_TF[2] << " " << "\n";
		
		
		i = i+1;
		usleep(forceSleepTime);
		
		
	}	
	
	//stopFT(&forceID);
	
	forcelog.close();
}

