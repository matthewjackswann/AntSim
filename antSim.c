#include <gtk/gtk.h>
#include <gtk/gtkmain.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

const int r = 1;
const int blurFrequency = 1000;
const double blurReductionFactor = 1.1;
const char antValue = 3;
const int baseWeight = 1;
const int forwardBias = 4; // 4x more likely to go in a straight line
const int antViewRadius = 2;
const int tickRate = 50; // ms to wait between ticks

const unsigned char wallR = 150;
const unsigned char wallG = 95;
const unsigned char wallB = 57;
const unsigned char antR = 72;
const unsigned char antG = 161;
const unsigned char antB = 233;
const unsigned char homeR = 105;
const unsigned char homeG = 255;
const unsigned char homeB = 105;
const unsigned char foodR = 235;
const unsigned char foodG = 52;
const unsigned char foodB = 186;
const unsigned char backgroundR = 176;
const unsigned char backgroundG = 135;
const unsigned char backgroundB = 107;


GdkPixbuf *create_pixbuf(const gchar *filename) {
   GdkPixbuf *pixbuf;
   GError *error = NULL;
   pixbuf = gdk_pixbuf_new_from_file(filename, &error);
   if (!pixbuf) {
      fprintf(stderr, "%s\n", error->message);
      g_error_free(error);
   }
   return pixbuf;
}

struct ant{
    char direction;
    bool withFood;
    int x;
    int y;
} typedef Ant;

struct simulation{
    guchar *data;
    guchar *background;
    int width;
    int height;
    int antNo;
    int maxAntNo;
    Ant *ants;
    GtkWidget *image;
    int blurTick;
    long tick;
} typedef Simulation;

struct pair {
    int first;
    int second;
} typedef pair;

void freeOnQuit(GtkWidget *widget, gpointer data) {
    free(data);
}

unsigned char *readFileBytes(const char *name) {
    FILE *fl = fopen(name, "rb");
    fseek(fl, 0, SEEK_END);
    const unsigned long len = ftell(fl);
    if (len < 54) {
        fprintf(stderr,".bmp file is too short to be valid\n");
        exit(EXIT_FAILURE);
    }
    unsigned char *ret = malloc(len);
    fseek(fl, 0, SEEK_SET);
    fread(ret, 1, len, fl);
    fclose(fl);
    if (ret[0] != 0x42 || ret[1] != 0x4d) {
        free(ret);
        fprintf(stderr,".bmp file header should start with 0x42 0x4d\n");
        exit(EXIT_FAILURE);
    }
    const unsigned long storedLength = ret[2] | (ret[3] << 8) | (ret[4] << 16) | (ret[5] << 24);
    if (storedLength != len) {
        free(ret);
        fprintf(stderr,".bmp file length != actual length\n");
        exit(EXIT_FAILURE);
    }
    return ret;
}

void addAnts(Simulation *s, int maxAnts, int startingAnts, int x, int y) {
    s->maxAntNo = maxAnts;
    s->antNo = startingAnts;
    s->ants = calloc(maxAnts, sizeof(Ant));
    for (int i = 0; i < startingAnts; i++) {
        s->ants[i] = (Ant) {(char) (i % 8), FALSE, x, y};
    }
}

// blurs the pheramone data stored in the simulation useing a box blur
void blurData(Simulation *s, int r) {
    // stores the data once blured in the horizontal direction
    guchar *hBlur = malloc(s->width * s->height * 3 * sizeof(guchar));
    for (int channel = 0; channel < 3; channel++) {
        for (int y = 0; y < s->height; y++) { // blurs in the horizontal direction
            for (int x = 0; x < s->width; x++) {
                int total = 0;
                for (int dx = -r; dx <= r; dx++) {
                    int xpos = (x + dx + s->width) % s->width;
                    total += s->data[3*(y*s->width+xpos)+channel];
                }
                hBlur[3*(y*s->width+x)+channel] = total / (2*r+1);
            }
        }
        for (int x = 0; x < s->width; x++) { // blurs in the vertical direction
            for (int y = 0; y < s->height; y++) {
                int total = 0;
                for (int dy = -r; dy <= r; dy++) {
                    int ypos = (y + dy + s->height) % s->height;
                    total += hBlur[3*(ypos*s->width+x)+channel];
                }
                s->data[3*(y*s->width+x)+channel] = total / ((2*r+1) * blurReductionFactor);
            }
        }
    }
}

// creates the initial state of the simulation
Simulation createSimulation(unsigned char *file_contents, int maxAnts, int startingAnts) {
    const unsigned long dibHeaderLength = file_contents[14] | (file_contents[15] << 8) | (file_contents[16] << 16) | (file_contents[17] << 24);
    if (dibHeaderLength != 40) {
        free(file_contents);
        fprintf(stderr,"only .bmp files with BITMAPINFOHEADER are supported\n");
        exit(EXIT_FAILURE);
    }
    const int width = file_contents[18] | (file_contents[19] << 8) | (file_contents[20] << 16) | (file_contents[21] << 24);
    if (width < 1) {
        free(file_contents);
        fprintf(stderr,".bmp file cannot have zero or negative width\n");
        exit(EXIT_FAILURE);
    }
    const int height = file_contents[22] | (file_contents[23] << 8) | (file_contents[24] << 16) | (file_contents[25] << 24);
    if (height < 1) {
        free(file_contents);
        fprintf(stderr,"only .bmp files with positive height are supported\n");
        exit(EXIT_FAILURE);
    }
    if (file_contents[26] != 1 || file_contents[27] != 0) {
        free(file_contents);
        fprintf(stderr,"only .bmp files with a colour plane of 1 are supported\n");
        exit(EXIT_FAILURE);
    }
    if (file_contents[28] != 24 || file_contents[29] != 0) {
        free(file_contents);
        fprintf(stderr,"only .bmp files with 24 bits per pixel (bpp) are supported\n");
        exit(EXIT_FAILURE);
    }
    if (file_contents[30] != 0 || file_contents[31] != 0 || file_contents[32] != 0 ||file_contents[33] != 0) {
        free(file_contents);
        fprintf(stderr,"only .bmp files with a compression method of BI_RGB are supported\n");
        exit(EXIT_FAILURE);
    }

    const unsigned long length = file_contents[2] | (file_contents[3] << 8) | (file_contents[4] << 16) | (file_contents[5] << 24);
    const int startOfData = file_contents[10] | (file_contents[11] << 8) | (file_contents[12] << 16) | (file_contents[13] << 24);
    const int rowsize = 4 * (((24 * width)+31) / 32) ;
    printf("%d\n", rowsize);
    if (startOfData + ((height - 1) * 3) + ((width - 1) * 3) > length - 1) {
        free(file_contents);
        fprintf(stderr,".bmp doesn't have a full pixel array\n");
        exit(EXIT_FAILURE);
    }

    Simulation simulation;
    simulation.width = width;
    simulation.height = height;
    int startingX = -1;
    int startingY = -1;
    guchar *background = malloc(width * height * 3 * sizeof(guchar));
    guchar *data = calloc(width * height * 3, sizeof(guchar));
    // the data from .bmp needs padding removed and converted to RGB
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            unsigned long readBytePos = startOfData + (y * rowsize) + (3 * x);
            unsigned long saveBytePos = ((height - y - 1) * width * 3) + (3 * x);
            background[saveBytePos + 0] = file_contents[readBytePos + 2];
            background[saveBytePos + 1] = file_contents[readBytePos + 1];
            background[saveBytePos + 2] = file_contents[readBytePos + 0];
            if (background[saveBytePos] == homeR && background[saveBytePos + 1] == homeG && background[saveBytePos + 2] == homeB) {
                startingX = x;
                startingY = (height - y - 1);
            }
        }
    }
    printf("w:%d, h:%d\n", width, height);
    if (startingX == -1 || startingY == -1) {
        free(file_contents);free(background);free(data);
        fprintf(stderr,"No home found on the map, mark it with RGB(105, 255, 105)\n");
        exit(EXIT_FAILURE);
    }
    simulation.background = background;
    simulation.data = data;
    simulation.blurTick = blurFrequency;
    simulation.tick = 0;
    printf("Starting Pos:%d, %d\n", startingX, startingY);
    addAnts(&simulation, maxAnts, startingAnts, startingX, startingY); // todo make starting and max modifiable // todo 200 breaks
    return simulation;
}

// creates a pixbuf image from the simulation. This is then used
// to display the current state to the gtk window
GdkPixbuf *getViewOfSimulation(Simulation *s) {
    GdkPixbuf *pic1;
    guchar *result = malloc(s->width * s->height * 3 * sizeof(guchar));
    for (long i = 0; i < s->width * s->height; i++) {
        if (s->background[3*i] == wallR && s->background[3*i+1] == wallG && s->background[3*i+2] == wallB) {
            result[3*i] = wallR;
            result[3*i+1] = wallG;
            result[3*i+2] = wallB;
        } else {
            for (int di = 0; di < 3; di++) { // for each channel
                float transparency = s->data[3*i+di] / 255.0;
                // result[3*i+di] = 255 * transparency + s->background[3*i+di] * (1 - transparency);
                result[3*i+di] = s->data[3*i+di] + s->background[3*i+di] * (1 - transparency);
            }
        }
    }
    for (int a = 0; a < s->antNo; a++) {
        Ant ant = s->ants[a];
        result[3*(s->width*ant.y+ant.x)] = antR;
        result[3*(s->width*ant.y+ant.x)+1] = antG;
        result[3*(s->width*ant.y+ant.x)+2] = antB;
    }
    pic1 = gdk_pixbuf_new_from_data(result, GDK_COLORSPACE_RGB, FALSE, 8, s->width, s->height, s->width * 3, NULL, NULL);
    free(result);
    return pic1;
}

bool isWall(guchar *background, int x, int y, int rowsize) {
    return background[3*(y*rowsize+x)] == wallR && background[3*(y*rowsize+x)+1] == wallG && background[3*(y*rowsize+x)+2] == wallB;
}

bool isFood(guchar *background, int x, int y, int rowsize) {
    return background[3*(y*rowsize+x)] == foodR && background[3*(y*rowsize+x)+1] == foodG && background[3*(y*rowsize+x)+2] == foodB;
}

bool isHome(guchar *background, int x, int y, int rowsize) {
    return background[3*(y*rowsize+x)] == homeR && background[3*(y*rowsize+x)+1] == homeG && background[3*(y*rowsize+x)+2] == homeB;
}

bool isValidDirection(guchar *background, int x, int y, bool hasFood, int rowsize, char direction) {
    switch (direction) {
        case 0: return !isWall(background, x, y-1, rowsize) && (!hasFood || !isFood(background, x, y-1, rowsize));
        case 1: return !isWall(background, x+1, y-1, rowsize) && (!hasFood || !isFood(background, x+1, y-1, rowsize));
        case 2: return !isWall(background, x+1, y, rowsize) && (!hasFood || !isFood(background, x+1, y, rowsize));
        case 3: return !isWall(background, x+1, y+1, rowsize) && (!hasFood || !isFood(background, x+1, y+1, rowsize));
        case 4: return !isWall(background, x, y+1, rowsize) && (!hasFood || !isFood(background, x, y+1, rowsize));
        case 5: return !isWall(background, x-1, y+1, rowsize) && (!hasFood || !isFood(background, x-1, y+1, rowsize));
        case 6: return !isWall(background, x-1, y, rowsize) && (!hasFood || !isFood(background, x-1, y, rowsize));
        case 7: return !isWall(background, x-1, y-1, rowsize) && (!hasFood || !isFood(background, x-1, y-1, rowsize));
    }
    fprintf(stderr, "Can't have a direction %d", direction);
    exit(EXIT_FAILURE);
}

// for increased ant view radius the top left x y is used
pair getTopXY(int x, int y, int direction) {
    switch (direction) {
        case 0: return (pair) {x - antViewRadius, y - 1 - 2 * antViewRadius};
        case 1: return (pair) {x + 1, y - 1 - 2 * antViewRadius};
        case 2: return (pair) {x + 1, y - antViewRadius};
        case 3: return (pair) {x + 1, y + 1};
        case 4: return (pair) {x - antViewRadius, y + 1};
        case 5: return (pair) {x - 1 - 2 * antViewRadius, y + 1};
        case 6: return (pair) {x - 1 - 2 * antViewRadius, y - antViewRadius};
        case 7: return (pair) {x - 1 - 2 * antViewRadius, y - 1 - 2 * antViewRadius};
    }
    fprintf(stderr,"Can't have a direction of d:%d", direction);
    exit(EXIT_FAILURE);
}

// gives the wieght of a direction
int getWeight(Simulation *s, Ant *ant, char direction) {
    int offset = ant->withFood ? 1 : 0; // which  pheromones to follow
    pair top = getTopXY(ant->x, ant->y, ant->direction);
    int totalWeight = baseWeight;
    for (int dy = 0; dy < antViewRadius; dy++) {
        for (int dx = 0; dx < antViewRadius; dx++) {
            int resX = (top.first + dx + s->width) % s->width;
            int resY = (top.second + dy + s->height) % s->height;
            totalWeight += s->data[3*(resY*s->width+resX)+offset];
        }
    }
    if (totalWeight <= 0) {
        fprintf(stderr,"cant's have negative weight w:%d", totalWeight);
        exit(EXIT_FAILURE);
    }
    return totalWeight;
}

// returns the index of the chosen direction
int weightedRandom(int *weights) {
    int sumOfWeights = weights[0] + weights[1] + weights[2];
    int random = rand() % (sumOfWeights) + 1;
    int irandom = random;
    for (int i = 0; i <= 3; i++) {
        random -= weights[i];
        if(random <= 0) {
            return i;
        }
    }
    fprintf(stderr,"weightedRandom broke, shouldn't ever happen (r: %d, ir: %d w:%d, %d, %d)", random, irandom, weights[0], weights[1], weights[2]);
    exit(EXIT_FAILURE);
}

// gets the list of directions an ant can move in, -1 if there is no valid
// move for the given index
char getAntDirection(Simulation *s, Ant *ant) {
    char validDirections[3];
    for (int d = 0; d < 3; d++) {
        if (isValidDirection(s->background, ant->x, ant->y, ant->withFood, s->width, (ant->direction + d + 7) % 8)) {
            validDirections[d] = (ant->direction + d + 7) % 8;
        } else if (isValidDirection(s->background, ant->x, ant->y, ant->withFood, s->width, (ant->direction + d + 3) % 8)) {
            validDirections[d] = (ant->direction + d + 3) % 8;
        } else {
            validDirections[d] = -1;
        }
    }
    int weights[3];
    for (int d = 0; d < 3; d++) {
        if (validDirections[d] < 0) {
            weights[d] = 0;
        } else if (d == 1){
            weights[d] = forwardBias * getWeight(s, ant, validDirections[d]);
        } else {
            weights[d] = getWeight(s, ant, validDirections[d]);
        }
    }
    return validDirections[weightedRandom(weights)];
}

// moves an ant one tick in the simulation
void moveAnt(Simulation *s, Ant *ant) {
    char newDirection = getAntDirection(s, ant);
    switch (newDirection) {
        case 0: ant->y -= 1; break;
        case 1: ant->y -= 1; ant->x += 1; break;
        case 2: ant->x += 1; break;
        case 3: ant->y += 1; ant->x += 1; break;
        case 4: ant->y += 1; break;
        case 5: ant->y += 1; ant->x -= 1; break;
        case 6: ant->x -= 1; break;
        case 7: ant->y -=1; ant->x -= 1; break;
        default: fprintf(stderr,"ant can't have direction d:%d. (shouldn't ever get here)\n x:%d y:%d d:%d\n", newDirection, ant->x, ant->y, ant->direction);
        exit(EXIT_FAILURE);
    }
    ant->direction = newDirection;
    ant->y = (ant->y + s->height) % s->height;
    ant->x = (ant->x + s->width) % s->width;
    if (!ant->withFood && isFood(s->background, ant->x, ant->y, s->width)) {
        ant->withFood = true;
        s->background[3*(ant->y*s->width+ant->x)] = backgroundR;
        s->background[3*(ant->y*s->width+ant->x)+1] = backgroundG;
        s->background[3*(ant->y*s->width+ant->x)+2] = backgroundB;
    }
    if (ant->withFood && isHome(s->background, ant->x, ant->y, s->width)) {
        ant->withFood = false;
        if (s->antNo + 1 <= s->maxAntNo) { // if there is space for new ants
            s->ants[s->antNo] = (Ant) {rand() % 8, false, ant->x, ant->y};
            s->antNo++;
        }
    }
}

// called everytime the gtk has finished rendering. It simulates on tick
// in the simulation and then sleeps for tickRate ms
gboolean simulateTick(void *data) {
    Simulation *s = data;
    s->tick++;
    GdkPixbuf *pixbuf;
    // moves all ants
    for (int a = 0; a < s->antNo; a++) {
        Ant *ant = &s->ants[a];
        int offset = ant->withFood ? 0 : 1; // which  pheromones to leave
        guchar newPheramone;
        if ((s->data[3*(ant->y*s->width+ant->x)+offset] + antValue) > 255) {
            newPheramone = 255;
        } else {
            newPheramone = (s->data[3*(ant->y*s->width+ant->x)+offset] + antValue);
        }
        s->data[3*(ant->y*s->width+ant->x)+offset] = newPheramone;
        moveAnt(s, ant);
    }
    // blurs pheromones
    if (s->tick % s->blurTick == 0) {
        blurData(s, r);
    }
    // gets view of simulation
    pixbuf = getViewOfSimulation(s);
    gtk_image_set_from_pixbuf(GTK_IMAGE(s->image), pixbuf);
    g_object_unref(pixbuf);

    // time to wait between frames
    usleep(tickRate * 1000);

    return TRUE; // return FALSE to remove the timeout
}

int main(int argc, char *argv[]) {
    setbuf(stdout, NULL);
    // load a map from the .bmp image file
    unsigned char *file_contents = readFileBytes("test.bmp");
    Simulation s = createSimulation(file_contents, 5000, 500);
    free(file_contents);

    GtkWidget *window;
    GtkWidget *image;
    GdkPixbuf *icon;
    GdkPixbuf *pic1;
    gtk_init(&argc, &argv);
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    gtk_container_set_border_width(GTK_CONTAINER(window), 15);
    icon = create_pixbuf("icon.png");
    gtk_window_set_icon(GTK_WINDOW(window), icon);

    pic1 = getViewOfSimulation(&s);
    image = gtk_image_new_from_pixbuf(pic1);
    s.image = image;
    g_idle_add(simulateTick, &s);
    gtk_container_add(GTK_CONTAINER(window), image);
    gtk_widget_show_all(window);

    // frees any malloced memory on window close
    g_signal_connect(window, "destroy", G_CALLBACK(freeOnQuit), s.background);
    g_signal_connect(window, "destroy", G_CALLBACK(freeOnQuit), s.data);
    g_signal_connect(window, "destroy", G_CALLBACK(freeOnQuit), s.ants);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_object_unref(icon);
    g_object_unref(pic1);
    gtk_main();
    return 0;
}
