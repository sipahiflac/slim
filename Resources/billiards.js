// Three equal-mass balls, cushions and rolling friction. One visible frame per 100 ms.
// Ten small physics steps per frame keep the small LCD balls from tunnelling.
class HandsetBilliards {
    constructor(){this.reset();}
    reset(){
        this.radius=3;this.bounds={left:6,right:89,top:14,bottom:55};
        this.balls=[{x:24,y:34,vx:0,vy:0},{x:63,y:22,vx:0,vy:0},{x:69,y:44,vx:0,vy:0}].map(ball=>({...ball,rollX:0,rollY:0,spin:0}));
        this.angle=340;this.power=6;this.streak=0;this.mode='angle';this.paused=false;
        this.tipX=0;this.tipY=0;this.tipEditing=false;
        this.charging=false;this.chargeStarted=0;this.meter=0;this.displayedMeterFill=0;this.meterWidth=44;this.meterTarget=.78;this.perfectUntil=0;this.perfectShakeFrames=0;
        this.contacts=new Set();this.result='';
    }
    adjust(delta,fine=false){
        if(this.mode==='rolling')return;
        this.result='';
        if(this.mode==='angle')this.angle=(this.angle+delta*(fine?1:5)+360)%360;
        else this.power=Math.max(1,Math.min(10,this.power+delta));
    }
    setTip(x,y){
        const length=Math.hypot(x,y),scale=length>.9?.9/length:1;
        this.tipX=x*scale;this.tipY=y*scale;
    }
    startCharge(){
        if(this.mode!=='power'||this.charging)return false;
        this.charging=true;this.chargeStarted=performance.now();this.meter=0;this.displayedMeterFill=0;return true;
    }
    updateCharge(){
        if(!this.charging)return;
        const phase=((performance.now()-this.chargeStarted)%1500)/750;
        this.meter=phase<=1?phase:2-phase;
    }
    cancelCharge(){this.charging=false;this.meter=0;this.displayedMeterFill=0;}
    meterFill(){return Math.round(this.meter*this.meterWidth);}
    targetFill(){return Math.round(this.meterTarget*this.meterWidth);}
    drawMeterFill(){
        this.updateCharge();
        const next=this.meterFill(),last=this.displayedMeterFill,target=this.targetFill();
        // At 10 FPS the continuous sweep can skip the one-pixel target.
        // Present one exact target frame on each crossing, then carry on.
        const crossed=this.charging&&((last<target&&next>target)||(last>target&&next<target));
        this.displayedMeterFill=crossed?target:next;
        return this.displayedMeterFill;
    }
    releaseCharge(){
        if(!this.charging)return false;
        // Judge what the player could see, never a hidden timestamp ahead of it.
        this.meter=this.displayedMeterFill/this.meterWidth;this.charging=false;
        const deviation=Math.abs(this.meter-this.meterTarget),perfect=this.displayedMeterFill===this.targetFill();
        this.power=perfect?10:Math.min(9.2,1+8.2*Math.max(0,1-deviation/this.meterTarget));
        this.perfectUntil=perfect?performance.now()+1000:0;
        this.perfectShakeFrames=perfect?1:0;
        this.shoot();return true;
    }
    shoot(){
        if(this.mode==='rolling')return;
        const radians=this.angle*Math.PI/180;
        // Keep delivered kinetic energy bounded as an off-centre impulse adds rotation.
        const speed=.675*(45+this.power*18)/Math.sqrt(1+2.5*(this.tipX*this.tipX+this.tipY*this.tipY));
        const dx=Math.cos(radians),dy=Math.sin(radians),cue=this.balls[0];
        cue.vx=dx*speed;cue.vy=dy*speed;
        // Solid sphere inertia I=2mR²/5. Upward contact gives follow; downward gives draw.
        cue.rollX=-2.5*this.tipY*speed*dx;cue.rollY=-2.5*this.tipY*speed*dy;
        cue.spin=-2.5*this.tipX*speed/this.radius;
        this.contacts.clear();this.mode='rolling';this.paused=false;this.result='';
    }
    tick(){
        if(this.mode!=='rolling'||this.paused)return;
        const dt=.01;
        for(let step=0;step<10;step++){
            for(const ball of this.balls){
                ball.x+=ball.vx*dt;ball.y+=ball.vy*dt;
                this.cushion(ball);
            }
            for(let i=0;i<3;i++)for(let j=i+1;j<3;j++)this.collide(i,j);
            for(const ball of this.balls){
                // Collision correction can move a ball onto a cushion.
                this.cushion(ball);
                this.cloth(ball,dt);
            }
        }
        if(this.balls.every(ball=>Math.hypot(ball.vx,ball.vy)<.35&&Math.hypot(ball.rollX,ball.rollY)<.35)){
            this.balls.forEach(ball=>{ball.vx=0;ball.vy=0;ball.rollX=0;ball.rollY=0;ball.spin=0;});
            const scored=this.contacts.has(1)&&this.contacts.has(2);
            this.streak=scored?this.streak+1:0;this.result=scored?'+1':'Miss';this.mode='angle';
        }
    }
    cloth(ball,dt){
        const sx=ball.vx-ball.rollX,sy=ball.vy-ball.rollY,slip=Math.hypot(sx,sy);
        if(slip>.01){
            // Sliding friction reduces slip 7/2 times faster than it changes linear speed.
            const change=Math.min(30*dt,slip/3.5),dx=change*sx/slip,dy=change*sy/slip;
            ball.vx-=dx;ball.vy-=dy;ball.rollX+=2.5*dx;ball.rollY+=2.5*dy;
        }else{
            const speed=Math.hypot(ball.vx,ball.vy),remaining=Math.max(0,speed-18*dt);
            if(speed>0){ball.vx*=remaining/speed;ball.vy*=remaining/speed;}
            ball.rollX=ball.vx;ball.rollY=ball.vy;
        }
        ball.spin*=Math.exp(-dt*.5);
    }
    rail(ball,nx,ny){
        const normal=ball.vx*nx+ball.vy*ny;if(normal>=0)return;
        const impulse=-(1+.92)*normal;
        ball.vx+=impulse*nx;ball.vy+=impulse*ny;
        const tx=-ny,ty=nx,slip=ball.vx*tx+ball.vy*ty-ball.spin*this.radius;
        const tangent=Math.max(-.18*impulse,Math.min(.18*impulse,-slip/3.5));
        ball.vx+=tangent*tx;ball.vy+=tangent*ty;ball.spin-=2.5*tangent/this.radius;
    }
    cushion(ball){
        const b=this.bounds;
        if(ball.x<b.left){ball.x=b.left;this.rail(ball,1,0);}
        if(ball.x>b.right){ball.x=b.right;this.rail(ball,-1,0);}
        if(ball.y<b.top){ball.y=b.top;this.rail(ball,0,1);}
        if(ball.y>b.bottom){ball.y=b.bottom;this.rail(ball,0,-1);}
    }
    collide(i,j){
        const a=this.balls[i],b=this.balls[j],dx=b.x-a.x,dy=b.y-a.y;
        const distance=Math.hypot(dx,dy),diameter=this.radius*2;
        if(distance>=diameter)return;
        const nx=distance>1e-8?dx/distance:1,ny=distance>1e-8?dy/distance:0;
        const separation=(diameter-distance)/2+.0001;
        a.x-=nx*separation;a.y-=ny*separation;b.x+=nx*separation;b.y+=ny*separation;
        const relative=(b.vx-a.vx)*nx+(b.vy-a.vy)*ny;
        if(relative>=0)return;
        const impulse=-(1+.98)*relative/2;
        a.vx-=impulse*nx;a.vy-=impulse*ny;b.vx+=impulse*nx;b.vy+=impulse*ny;
        const tx=-ny,ty=nx,slip=(b.vx-a.vx)*tx+(b.vy-a.vy)*ty-this.radius*(a.spin+b.spin);
        const tangent=Math.max(-.04*impulse,Math.min(.04*impulse,-slip/7));
        a.vx-=tangent*tx;a.vy-=tangent*ty;b.vx+=tangent*tx;b.vy+=tangent*ty;
        a.spin-=2.5*tangent/this.radius;b.spin-=2.5*tangent/this.radius;
        if(i===0)this.contacts.add(j);
    }
}
