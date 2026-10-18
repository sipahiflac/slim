// A quiet, grid-exact handset game. No animation or timer runs outside its screen.
class HandsetSnake {
    constructor(){
        this.cols=30;this.rows=15;this.best=0;
        try{this.best=Math.max(0,Number(localStorage.getItem('2g.snake.best'))||0);}catch(_){}
        this.reset();
    }
    reset(){
        this.body=Array.from({length:5},(_,i)=>({x:12-i,y:7}));
        this.direction={x:1,y:0};this.turns=[];this.heldDirections=new Set();this.score=0;this.status='ready';this.placeFood();
    }
    get boosted(){return this.heldDirections.has(`${this.direction.x},${this.direction.y}`);}
    get delay(){const normal=Math.max(90,190-this.score*4);return this.boosted?Math.max(55,Math.round(normal*.55)):normal;}
    turn(x,y){
        if(this.status!=='playing')return false;
        const previous=this.direction;
        if(previous.x===x&&previous.y===y||previous.x===-x&&previous.y===-y)return false;
        this.direction={x,y};return true;
    }
    placeFood(){
        const empty=[];
        for(let y=0;y<this.rows;y++)for(let x=0;x<this.cols;x++)
            if(!this.body.some(cell=>cell.x===x&&cell.y===y))empty.push({x,y});
        this.food=empty.length?empty[Math.floor(Math.random()*empty.length)]:null;
        if(!this.food)this.status='won';
    }
    step(){
        if(this.status!=='playing')return;
        const head={x:(this.body[0].x+this.direction.x+this.cols)%this.cols,y:(this.body[0].y+this.direction.y+this.rows)%this.rows};
        const eating=this.food&&head.x===this.food.x&&head.y===this.food.y;
        const occupied=eating?this.body:this.body.slice(0,-1);
        if(occupied.some(cell=>cell.x===head.x&&cell.y===head.y)){
            this.status='over';return;
        }
        this.body.unshift(head);
        if(eating){
            this.score++;
            if(this.score>this.best){this.best=this.score;try{localStorage.setItem('2g.snake.best',String(this.best));}catch(_){} }
            this.placeFood();
        }else this.body.pop();
    }
    static icon(){
        const pixels=Array.from({length:32},()=>Array(32).fill(0));
        const widths=[[8,23],[6,25],[5,26],[4,27],[3,28],[3,28],[3,28],[3,28],[3,28],[3,28],[3,28],[3,28],[4,27],[4,27],[5,12],[6,11]];
        widths.forEach(([left,right],row)=>{
            const y=8+row;
            for(let x=left;x<=right;x++)pixels[y][x]=x===left||x===right||row===0?1:row>11?2:7;
        });
        for(let y=22;y<24;y++)for(let x=21;x<27;x++)pixels[y][x]=y===23?1:2;
        for(let y=12;y<19;y++)for(let x=7;x<14;x++)if(x>=9&&x<=11||y>=14&&y<=16)pixels[y][x]=1;
        [[21,13],[25,17]].forEach(([cx,cy])=>{
            for(let y=-1;y<=1;y++)for(let x=-1;x<=1;x++)if(x===0||y===0)pixels[cy+y][cx+x]=1;
        });
        for(let x=6;x<26;x++)pixels[20][x]=2;
        return pixels;
    }
}
