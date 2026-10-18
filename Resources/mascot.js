// The two 32 x 32 pixel poses supplied for the handset: resting, then watching.
// "." is transparent LCD glass; the other values retain the source art's
// outline, two shadow tones, and beak highlight in the active LCD palette.
const MASCOT_FRAMES = [
  [
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "..........#######...............",
    ".........#s......#..............",
    "........#S........#.............",
    ".......#Ss.........#............",
    ".......#Ss..#..#...#............",
    ".......#SSs.###....#............",
    "......#####........#............",
    ".....#hhhh#........#............",
    "......####s........#............",
    ".......#SSs........s#...........",
    ".......#SSs.........s####.......",
    ".......#SSs..........ssss#......",
    "......#SSs.......####....s#.....",
    "......#SSs...........#...s#.....",
    "......#SSs..........#s..s#......",
    "......#SSs.......###s...s#......",
    ".......#SSs......sss...s#.......",
    "........################........",
    "........SSSSSSSSSSSSSSSS........",
    "........ssssssssssssssss........",
    ".........ssssssssssssss.........",
    "...........ssssssssss...........",
    "..............sss...............",
    "................................",
    "................................",
    "................................"
  ],
  [
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "..........#######...............",
    ".........#Ss.....#..............",
    ".........#Ss.###..#.............",
    "........#SSs.......#............",
    "........#SSs..#....#............",
    "........#SSs.......#............",
    "........####.......#............",
    ".......#hhh#.......#............",
    "........###s.......#............",
    "........#SSs.......#............",
    ".......#SSs........#............",
    ".......#SSs.........s####.......",
    ".......#SSs..........ssss#......",
    "......#SSs.......####....s#.....",
    "......#SSs...........#...s#.....",
    "......#SSs..........#s..s#......",
    "......#SSs.......###s...s#......",
    ".......#SSs......sss...s#.......",
    "........################........",
    "........SSSSSSSSSSSSSSSS........",
    "........ssssssssssssssss........",
    ".........ssssssssssssss.........",
    "...........ssssssssss...........",
    "..............sss...............",
    "................................",
    "................................",
    "................................"
  ]
];

// Breathing in: the resting pose with everything above the belly lifted one pixel
// while the base stays planted, so the last belly row repeats to fill the gap.
const MASCOT_BREATH_FRAME = MASCOT_FRAMES[0].map((row, y) => y >= 5 && y <= 21 ? MASCOT_FRAMES[0][y + 1] : row);

const MASCOT_GLITCH_FRAME = [
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "..........Ssbbbbb...............",
    "..........Ssbbbbbb..............",
    ".........SSsbbbbbbb.............",
    ".........SSsbb.bbbb.............",
    ".........SSsbb.bbbb.............",
    "............bb.bbbb.............",
    "............bb.bbbb.............",
    "..........h.bb.bbbb.............",
    ".........hh.bb.bbbb.............",
    "............bb.bbbb.............",
    ".........SSsbb.bbbbbs...........",
    "........SSsbb...bbbbbssss.......",
    ".......SSbbbb...b....bbbbs......",
    ".......SSbbbbbbbbbbbb.bbbs......",
    ".......SSbbbbbbbbbbb.sbbs.......",
    ".......SSbbbbbbbb...sbbbs.......",
    "........SSsbbbbbbsssbbbs........",
    "................................",
    "........dddddddddddddddd........",
    ".........dddddddddddddd.........",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................"
];
function mascotBodyOrigin(frames,bodyPixel){
    let minX=32,minY=32,maxX=-1,maxY=-1;
    frames.forEach(rows=>rows.forEach((row,y)=>{
        for(let x=0;x<row.length;x++)if(bodyPixel(row[x],y)){
            minX=Math.min(minX,x);maxX=Math.max(maxX,x);
            minY=Math.min(minY,y);maxY=Math.max(maxY,y);
        }
    }));
    return {
        x:Math.floor((96-(maxX-minX+1))/2)-minX,
        y:Math.floor((73-(maxY-minY+1))/2)-minY
    };
}
const MASCOT_ORIGIN=mascotBodyOrigin(MASCOT_FRAMES,(pixel,y)=>pixel!=='.'&&y<24);
const MASCOT_GLITCH_ORIGIN=mascotBodyOrigin([MASCOT_GLITCH_FRAME],pixel=>pixel!=='.'&&pixel!=='d');
function drawMascotPixels(ctx,rows,tones,origin){
    rows.forEach((row,y)=>{
        for(let x=0;x<row.length;x++){
            const tone=tones[row[x]];
            if(tone){ctx.fillStyle=tone;ctx.fillRect(origin.x+x,origin.y+y,1,1);}
        }
    });
}
class LcdMascot {
    draw(ctx,foreground,background,pose=0){
        const fg=[1,3,5].map(i=>parseInt(foreground.slice(i,i+2),16));
        const bg=[1,3,5].map(i=>parseInt(background.slice(i,i+2),16));
        const blend=t=>'rgb('+fg.map((value,i)=>Math.round(value+(bg[i]-value)*t)).join(',')+')';
        const highlight='rgb('+bg.map(value=>Math.round(value+(255-value)*0.175)).join(',')+')';
        const tones={'#':blend(0.15),'S':blend(0.59),'s':blend(0.875),'h':highlight};
        drawMascotPixels(ctx,pose===2?MASCOT_BREATH_FRAME:MASCOT_FRAMES[pose===1?1:0],tones,MASCOT_ORIGIN);
    }
    drawGlitch(ctx){
        drawMascotPixels(ctx,MASCOT_GLITCH_FRAME,{
            b:'#769267',S:'#526349',s:'#6b835e',h:'#8ea582',d:'#151a12'
        },MASCOT_GLITCH_ORIGIN);
    }
}
