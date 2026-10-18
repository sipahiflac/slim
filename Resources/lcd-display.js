// Configurable logical LCD rows, 2x optical output. Work only while a cell is changing.
class LcdDisplay {
    constructor(context,height=65) {
        this.height=height;
        this.context=context;
        this.target=new Uint8ClampedArray(96*this.height*4);
        this.state=new Float32Array(96*this.height*3);
        this.frame=context.createImageData(192,this.height*2);
        this.light=new Float32Array(96*this.height);
        this.glass=new Float32Array(96*this.height);
        this.edge=new Float32Array(96*this.height);
        this.dark=new Float32Array(96*this.height);
        this.flat=new Float32Array(3);
        this.lit=1;this.targetLit=1;
        this.ready=false;this.pending=0;this.lastTime=0;
        for(let y=0;y<this.height;y++) for(let x=0;x<96;x++) {
            const i=y*96+x;
            // Fixed light-guide distribution, calculated once. LEDs themselves
            // are behind the bezel; only their broad pools reach the panel.
            const left=Math.exp(-((x-9)**2/350+(y-(this.height-3))**2/650));
            const right=Math.exp(-((x-88)**2/420+(y-(this.height-3))**2/720));
            this.light[i]=-5+11*left+10*right-3*y/this.height;
            // The bezel stands proud of the glass and shades it, most under the top edge.
            this.edge[i]=1-.07*Math.exp(-x/3)-.05*Math.exp(-(95-x)/3.2)-.14*Math.exp(-y/3.5)-.04*Math.exp(-(this.height-1-y)/2.5);
            const reflection=Math.exp(-(((y+.23*x-11)/24)**2));
            const grain=(((x*73+y*151+x*y*3)%101)/100-.5)*.28;
            this.glass[i]=1.25*reflection+grain;
        }
        this.tick=t=>this.advance(t);
    }
    submit(pixels,backlight=true,immediate=false) {
        // Menu blanking must erase old glyphs completely before its timed gap.
        if(immediate){
            if(this.pending)cancelAnimationFrame(this.pending);
            this.pending=0;this.ready=false;
        }
        const nextLight=backlight?1:0;
        let changed=!this.ready || nextLight!==this.targetLit;
        this.targetLit=nextLight;
        for(let i=0;i<pixels.length;i++) if(this.target[i]!==pixels[i]) {changed=true;break;}
        if(!changed)return;
        this.target.set(pixels);
        if(!this.ready) {
            for(let i=0;i<96*this.height;i++) for(let c=0;c<3;c++) this.state[i*3+c]=pixels[i*4+c];
            this.lit=this.targetLit;this.ready=true;this.paint();return;
        }
        if(!this.pending) {this.lastTime=performance.now();this.pending=requestAnimationFrame(this.tick);}
    }
    advance(time) {
        this.pending=0;
        const dt=time-this.lastTime;
        if(dt<1000/30) {this.pending=requestAnimationFrame(this.tick);return;}
        this.lastTime=time;
        const fast=1-Math.exp(-Math.min(dt,100)/32);
        const slow=1-Math.exp(-Math.min(dt,100)/65);
        // Pixel response is the LCD's own physics, not interface animation, so it ignores 'reduce motion'.
        const immediate=false;
        let moving=false;
        const lightDelta=this.targetLit-this.lit;
        if(Math.abs(lightDelta)<.005 || immediate)this.lit=this.targetLit;
        else {this.lit+=lightDelta*(1-Math.exp(-Math.min(dt,100)/55));moving=true;}
        for(let i=0;i<96*this.height;i++) {
            const p=i*3,q=i*4;
            const turningOn=this.target[q]+this.target[q+1]+this.target[q+2]<this.state[p]+this.state[p+1]+this.state[p+2];
            const a=immediate?1:turningOn?fast:slow;
            for(let c=0;c<3;c++) {
                const delta=this.target[q+c]-this.state[p+c];
                if(Math.abs(delta)<0.35 || immediate)this.state[p+c]=this.target[q+c];
                else {this.state[p+c]+=delta*a;moving=true;}
            }
        }
        this.paint();
        if(moving)this.pending=requestAnimationFrame(this.tick);
    }
    paint() {
        const out=this.frame.data,s=this.state,dark=this.dark;
        // Darkness of each cell relative to this frame's own brightest cell, so the shadow
        // below works the same lit and unlit and in any palette.
        let top=0,flat=this.flat;flat[0]=flat[1]=flat[2]=0;
        for(let i=0;i<dark.length;i++){
            const p=i*3,lum=.3*s[p]+.59*s[p+1]+.11*s[p+2];dark[i]=lum;if(lum>top)top=lum;
            if(s[p]>flat[0])flat[0]=s[p];if(s[p+1]>flat[1])flat[1]=s[p+1];if(s[p+2]>flat[2])flat[2]=s[p+2];
        }
        const range=Math.max(40,top-50);
        for(let i=0;i<dark.length;i++)dark[i]=Math.max(0,Math.min(1,(top-dark[i])/range));
        for(let y=0;y<this.height;y++) for(let x=0;x<96;x++) {
            const i=y*96+x,p=i*3;
            // Dot-matrix shadow: ink falls on the glass behind it, down and to the right,
            // strongest in the sub-pixel nearest the cell that casts it (DMG-style depth).
            const cast=Math.min(1,.35*(x&&y?dark[i-97]:0)+.4*(x?dark[i-1]:0)+.4*(y?dark[i-96]:0));
            const shadow=.05*cast*(1-dark[i]);
            for(let sy=0;sy<2;sy++) for(let sx=0;sx<2;sx++) {
                // Optical spill across neighbouring cells, not a blurred font.
                const nx=Math.max(0,Math.min(95,x+(sx?1:-1)));
                const ny=Math.max(0,Math.min(this.height-1,y+(sy?1:-1)));
                const h=(y*96+nx)*3,v=(ny*96+x)*3;
                const q=((y*2+sy)*192+x*2+sx)*4;
                for(let c=0;c<3;c++) {
                    // Light leaks into the neighbouring cells through the diffuser, so edges are soft, not
                    // pixel-hard. No extra contrast push: the palette's ink is already dark enough, and a push
                    // stacked with the shadow turned dark areas (the menu icons) into near-black blobs.
                    // The whole panel is 4% dimmer, which takes the glare off the green paper.
                    const spilled=.93*s[p+c]+.04*s[h+c]+.03*s[v+c];
                    const value=spilled*.96;
                    // Dot-matrix grid: a thin gap after every cell column and row, kept aligned
                    // to the logical cells so the 96 x 73 pixels stay crisp. No RGB fringe.
                    const aperture=(sx?.965:1)*(sy?.98:1)*1.02;
                    const transmission=.2+.8*value/255;
                    const lit=(value*aperture*this.edge[i]+this.light[i]*this.lit*transmission)
                        *(1-shadow*(sx?.7:1)*(sy?.7:1));
                    out[q+c]=lit+this.glass[i];
                }
                out[q+3]=255;
            }
        }
        this.context.putImageData(this.frame,0,0);
    }
}
if(typeof module!=='undefined')module.exports=LcdDisplay;
